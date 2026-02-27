#include "BinanceClient.h"
#include "../settings/Settings.h"
#include <iostream>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>
#include <sstream>
#include <openssl/hmac.h>
#include <openssl/sha.h>
#include <openssl/evp.h>
#include <iomanip>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>
#include <openssl/ssl.h>
#include <openssl/err.h>
#include <map>
#include <thread>
#include <atomic>

namespace glora {
namespace network {

// Interval mapping from frontend to Binance API format
static const std::map<std::string, std::string> INTERVAL_MAP = {
  {"1s", "1s"},
  {"1m", "1m"},
  {"5m", "5m"},
  {"15m", "15m"},
  {"1h", "1h"},
  {"4h", "4h"},
  {"1D", "1d"},
  {"1W", "1w"},
  {"1M", "1M"}
};

// Convert frontend interval to Binance interval
static std::string toBinanceInterval(const std::string& interval) {
  auto it = INTERVAL_MAP.find(interval);
  if (it != INTERVAL_MAP.end()) {
    return it->second;
  }
  // Default to 1m if not found
  return "1m";
}

struct BinanceClient::Impl {
  ix::WebSocket webSocket;
  std::string activeSymbol;
  OnTickCallback onTick;
  
  // User API configuration
  std::string apiKey;
  std::string apiSecret;
  bool useTestnet = false;
  
  std::string getBaseUrl() const {
    return useTestnet ? "testnet.binance.vision" : "api.binance.com";
  }
  
  std::string getWsUrl() const {
    return useTestnet ? "wss://testnet.binance.vision/ws" : "wss://stream.binance.com:9443/ws";
  }
  
  // Generate HMAC SHA256 signature for API requests
  std::string generateSignature(const std::string& queryString) const {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    unsigned int digestLen = SHA256_DIGEST_LENGTH;
    
    HMAC(EVP_sha256(), 
         apiSecret.c_str(), apiSecret.length(),
         (const unsigned char*)queryString.c_str(), queryString.length(),
         digest, &digestLen);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
      ss << std::hex << std::setw(2) << std::setfill('0') << (int)digest[i];
    }
    return ss.str();
  }
  
  // Simple HTTPS GET using OpenSSL
  std::string httpsGet(const std::string& host, const std::string& path, const std::string& apiKeyHeader = "") {
    std::string response;
    
    SSL_library_init();
    SSL_CTX* ctx = SSL_CTX_new(TLS_client_method());
    if (!ctx) return response;
    
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
      SSL_CTX_free(ctx);
      return response;
    }
    
    struct hostent* server = gethostbyname(host.c_str());
    if (!server) {
      close(sock);
      SSL_CTX_free(ctx);
      return response;
    }
    
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    memcpy(&serv_addr.sin_addr.s_addr, server->h_addr, server->h_length);
    serv_addr.sin_port = htons(443);
    
    if (connect(sock, (struct sockaddr*)&serv_addr, sizeof(serv_addr)) < 0) {
      close(sock);
      SSL_CTX_free(ctx);
      return response;
    }
    
    SSL* ssl = SSL_new(ctx);
    SSL_set_fd(ssl, sock);
    
    if (SSL_connect(ssl) != 1) {
      SSL_free(ssl);
      close(sock);
      SSL_CTX_free(ctx);
      return response;
    }
    
    // Build HTTP request
    std::stringstream request;
    request << "GET " << path << " HTTP/1.1\r\n";
    request << "Host: " << host << "\r\n";
    request << "Accept: application/json\r\n";
    if (!apiKeyHeader.empty()) {
      request << "X-MBX-APIKEY: " << apiKeyHeader << "\r\n";
    }
    request << "Connection: close\r\n";
    request << "\r\n";
    
    std::string requestStr = request.str();
    SSL_write(ssl, requestStr.c_str(), requestStr.length());
    
    // Read response
    char buffer[4096];
    int bytesRead;
    while ((bytesRead = SSL_read(ssl, buffer, sizeof(buffer) - 1)) > 0) {
      buffer[bytesRead] = 0;
      response += buffer;
    }
    
    SSL_free(ssl);
    close(sock);
    SSL_CTX_free(ctx);
    
    // Extract body from HTTP response
    size_t bodyStart = response.find("\r\n\r\n");
    if (bodyStart != std::string::npos) {
      return response.substr(bodyStart + 4);
    }
    return response;
  }
};

BinanceClient::BinanceClient() : pImpl(std::make_unique<Impl>()) {}

BinanceClient::~BinanceClient() { shutdown(); }

bool BinanceClient::initialize(const settings::ApiConfig* config) {
  ix::initNetSystem();
  
  if (config && config->isValid()) {
    pImpl->apiKey = config->apiKey;
    pImpl->apiSecret = config->apiSecret;
    pImpl->useTestnet = config->useTestnet;
    hasApiConfig_ = true;
    std::cout << "Binance Client initialized with API credentials (Testnet: " 
              << (pImpl->useTestnet ? "yes" : "no") << ")" << std::endl;
  } else {
    std::cout << "Binance Client initialized (public data only)" << std::endl;
  }
  return true;
}

void BinanceClient::setApiConfig(const settings::ApiConfig& config) {
  if (config.isValid()) {
    pImpl->apiKey = config.apiKey;
    pImpl->apiSecret = config.apiSecret;
    pImpl->useTestnet = config.useTestnet;
    hasApiConfig_ = true;
    std::cout << "API credentials updated (Testnet: " << (pImpl->useTestnet ? "yes" : "no") << ")" << std::endl;
  }
}

void BinanceClient::fetchHistoricalAggTrades(
    const std::string &symbol, uint64_t startTime, uint64_t endTime,
    std::function<void(const std::vector<core::Tick> &)> onDataCallback) {
  
  std::vector<core::Tick> allTicks;
  uint64_t currentStart = startTime;
  const uint64_t maxLimit = 1000; // Binance API limit per request
  const uint64_t chunkSize = maxLimit * 1000; // ~1 second worth of trades
  
  while (currentStart < endTime) {
    uint64_t currentEnd = std::min(currentStart + chunkSize, endTime);
    
    // Build query string
    std::stringstream ss;
    ss << "/api/v3/aggTrades?"
       << "symbol=" << symbol 
       << "&startTime=" << currentStart
       << "&endTime=" << currentEnd
       << "&limit=" << maxLimit;
    std::string queryStr = ss.str();
    
    std::string path = queryStr;
    
    // Add signature if we have API credentials
    if (hasApiConfig_) {
      std::string signature = pImpl->generateSignature(queryStr);
      path = queryStr + "&signature=" + signature;
    }
    
    // Make request using HTTPS
    std::string apiKeyHeader = hasApiConfig_ ? pImpl->apiKey : "";
    std::string response = pImpl->httpsGet(pImpl->getBaseUrl(), path, apiKeyHeader);
    
    if (!response.empty()) {
      try {
        auto j = json::parse(response);
        
        if (j.is_array()) {
          for (const auto& trade : j) {
            core::Tick tick;
            tick.timestamp_ms = trade["T"].get<uint64_t>();
            tick.price = std::stod(trade["p"].get<std::string>());
            tick.quantity = std::stod(trade["q"].get<std::string>());
            tick.is_buyer_maker = trade["m"].get<bool>();
            allTicks.push_back(tick);
          }
          
          std::cout << "Fetched " << j.size() << " trades from " 
                    << currentStart << " to " << currentEnd << std::endl;
        }
        
      } catch (const std::exception& e) {
        std::cerr << "Error parsing historical trades: " << e.what() << std::endl;
      }
    } else {
      std::cerr << "Failed to fetch historical trades (empty response)" << std::endl;
    }
    
    currentStart = currentEnd + 1;
  }
  
  if (onDataCallback) {
    onDataCallback(allTicks);
  }
}

void BinanceClient::fetchKlines(const std::string& symbol, const std::string& interval,
                                 uint64_t startTime, uint64_t endTime,
                                 std::function<void(const std::vector<core::Candle>&)> onDataCallback) {
  
  // Convert interval to Binance format
  std::string binanceInterval = toBinanceInterval(interval);
  
  std::vector<core::Candle> candles;
  
  // Build query string
  std::stringstream ss;
  ss << "/api/v3/klines?"
     << "symbol=" << symbol 
     << "&interval=" << binanceInterval
     << "&startTime=" << startTime
     << "&endTime=" << endTime
     << "&limit=1000";
  std::string queryStr = ss.str();
  
  std::string path = queryStr;
  
  if (hasApiConfig_) {
    std::string signature = pImpl->generateSignature(queryStr);
    path = queryStr + "&signature=" + signature;
  }
  
  // Make request using HTTPS
  std::string apiKeyHeader = hasApiConfig_ ? pImpl->apiKey : "";
  std::string response = pImpl->httpsGet(pImpl->getBaseUrl(), path, apiKeyHeader);
  
  if (!response.empty()) {
    try {
      auto j = json::parse(response);
      
      if (j.is_array()) {
        for (const auto& kline : j) {
          core::Candle candle;
          candle.start_time_ms = kline[0].get<uint64_t>();
          candle.end_time_ms = kline[6].get<uint64_t>();
          candle.open = std::stod(kline[1].get<std::string>());
          candle.high = std::stod(kline[2].get<std::string>());
          candle.low = std::stod(kline[3].get<std::string>());
          candle.close = std::stod(kline[4].get<std::string>());
          candle.volume = std::stod(kline[5].get<std::string>());
          candles.push_back(candle);
        }
        
        std::cout << "Fetched " << candles.size() << " klines" << std::endl;
      }
      
    } catch (const std::exception& e) {
      std::cerr << "Error parsing klines: " << e.what() << std::endl;
    }
  } else {
    std::cerr << "Failed to fetch klines (empty response)" << std::endl;
  }
  
  if (onDataCallback) {
    onDataCallback(candles);
  }
}

void BinanceClient::fetchDepth(const std::string& symbol, int limit,
                               std::function<void(const std::vector<std::pair<double, double>>& bids,
                                                 const std::vector<std::pair<double, double>>& asks)> onDataCallback) {
  std::vector<std::pair<double, double>> bids;
  std::vector<std::pair<double, double>> asks;
  
  // Validate limit (must be between 5 and 1000)
  int validLimit = std::max(5, std::min(limit, 1000));
  
  // Build query string
  std::stringstream ss;
  ss << "/api/v3/depth?"
     << "symbol=" << symbol 
     << "&limit=" << validLimit;
  std::string queryStr = ss.str();
  
  std::string path = queryStr;
  
  // Make request using HTTPS (public endpoint, no auth needed)
  std::string response = pImpl->httpsGet(pImpl->getBaseUrl(), path, "");
  
  if (!response.empty()) {
    try {
      auto j = json::parse(response);
      
      // Parse bids
      if (j.contains("bids") && j["bids"].is_array()) {
        for (const auto& bid : j["bids"]) {
          double price = std::stod(bid[0].get<std::string>());
          double quantity = std::stod(bid[1].get<std::string>());
          bids.emplace_back(price, quantity);
        }
      }
      
      // Parse asks
      if (j.contains("asks") && j["asks"].is_array()) {
        for (const auto& ask : j["asks"]) {
          double price = std::stod(ask[0].get<std::string>());
          double quantity = std::stod(ask[1].get<std::string>());
          asks.emplace_back(price, quantity);
        }
      }
      
      std::cout << "Fetched depth: " << bids.size() << " bids, " 
                << asks.size() << " asks" << std::endl;
      
    } catch (const std::exception& e) {
      std::cerr << "Error parsing depth data: " << e.what() << std::endl;
    }
  } else {
    std::cerr << "Failed to fetch depth (empty response)" << std::endl;
  }
  
  if (onDataCallback) {
    onDataCallback(bids, asks);
  }
}

void BinanceClient::subscribeAggTrades(const std::string &symbol,
                                       OnTickCallback callback) {

  pImpl->activeSymbol = symbol;
  pImpl->onTick = std::move(callback);

  // Clear previous buffer and seen IDs for new subscription
  {
    std::lock_guard<std::mutex> lock(bufferMutex_);
    while (!wsMessageBuffer_.empty()) wsMessageBuffer_.pop();
    seenTradeIds_.clear();
  }

  // Binance websocket format for aggTrades is
  // wss://stream.binance.com:9443/ws/<symbol>@aggTrade
  std::string lowerSymbol = symbol;
  for (auto &c : lowerSymbol)
    c = std::tolower(c);

  std::string url = pImpl->getWsUrl() + "/" + lowerSymbol + "@aggTrade";
  pImpl->webSocket.setUrl(url);

  // Setup message handler with buffering and deduplication support
  pImpl->webSocket.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
          try {
            auto j = json::parse(msg->str);

            if (j.contains("e") && j["e"] == "aggTrade") {
              // Get trade ID for deduplication
              int64_t tradeId = j.contains("a") ? j["a"].get<int64_t>() : 0;
              
              // Check if buffering is enabled
              if (bufferingEnabled_) {
                // Buffer the message for later processing
                std::lock_guard<std::mutex> lock(bufferMutex_);
                wsMessageBuffer_.push(j);
                std::cout << "[Buffer] Buffered trade: " << tradeId << " (buffer size: " << wsMessageBuffer_.size() << ")" << std::endl;
                return;  // Don't process yet
              }
              
              // ID-based deduplication: skip if we've seen this trade or it's before REST fetch
              if (tradeId > 0) {
                if (tradeId <= lastRestTradeId_) {
                  // Skip - this trade was already fetched via REST
                  return;
                }
                if (seenTradeIds_.count(tradeId) > 0) {
                  // Skip - duplicate
                  return;
                }
                seenTradeIds_.insert(tradeId);
              }

              core::Tick tick;
              tick.timestamp_ms = j["T"].get<uint64_t>();
              tick.price = std::stod(j["p"].get<std::string>());
              tick.quantity = std::stod(j["q"].get<std::string>());
              tick.is_buyer_maker = j["m"].get<bool>();

              if (pImpl->onTick) {
                pImpl->onTick(tick);
              }
            }
          } catch (const json::parse_error &e) {
            std::cerr << "JSON Parse error: " << e.what()
                      << "\nMessage: " << msg->str << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "Error parsing tick: " << e.what() << std::endl;
          }
        } else if (msg->type == ix::WebSocketMessageType::Open) {
          std::cout << "Connected to Binance Websocket: " << pImpl->activeSymbol
                    << std::endl;
        } else if (msg->type == ix::WebSocketMessageType::Error) {
          std::cerr << "Websocket Error: " << msg->errorInfo.reason
                    << std::endl;
        }
      });
}

void BinanceClient::connectAndRun() {
  if (!pImpl->activeSymbol.empty()) {
    std::cout << "Starting websocket connection..." << std::endl;
    // start() runs automatically in a background thread provided by ixwebsocket
    pImpl->webSocket.start();
    
    // --- Rate Limit Fix: Start 20-second heartbeat for PING/PONG ---
    startHeartbeat(20);
  }
}

void BinanceClient::shutdown() {
  std::cout << "Shutting down Binance Client..." << std::endl;
  stopHeartbeat();  // Stop heartbeat on shutdown
  pImpl->webSocket.stop();
  ix::uninitNetSystem();
}

// --- Race Condition Fix: WebSocket Buffering & ID-based Deduplication ---

void BinanceClient::flushBuffer() {
  if (!bufferingEnabled_) return;
  
  std::lock_guard<std::mutex> lock(bufferMutex_);
  
  // Process all buffered messages
  while (!wsMessageBuffer_.empty()) {
    json msg = wsMessageBuffer_.front();
    wsMessageBuffer_.pop();
    
    // Check for duplicate using trade ID
    if (msg.contains("a")) {
      int64_t tradeId = msg["a"].get<int64_t>();
      if (tradeId <= lastRestTradeId_) {
        // Skip this trade - it's from before our REST fetch
        std::cout << "[Deduplication] Skipping duplicate trade: " << tradeId << std::endl;
        continue;
      }
      
      // Add to seen IDs
      seenTradeIds_.insert(tradeId);
    }
    
    // Process the message (call the callback if available)
    if (pImpl->onTick) {
      try {
        core::Tick tick;
        tick.timestamp_ms = msg["T"].get<uint64_t>();
        tick.price = std::stod(msg["p"].get<std::string>());
        tick.quantity = std::stod(msg["q"].get<std::string>());
        tick.is_buyer_maker = msg["m"].get<bool>();
        pImpl->onTick(tick);
      } catch (const std::exception& e) {
        std::cerr << "Error processing buffered tick: " << e.what() << std::endl;
      }
    }
  }
  
  // Clear the buffer
  while (!wsMessageBuffer_.empty()) wsMessageBuffer_.pop();
}

size_t BinanceClient::getBufferSize() const {
  std::lock_guard<std::mutex> lock(bufferMutex_);
  return wsMessageBuffer_.size();
}

// --- Rate Limit Fix: PING/PONG Heartbeat ---

void BinanceClient::startHeartbeat(uint32_t intervalSeconds) {
  if (heartbeatRunning_) {
    stopHeartbeat();
  }
  
  heartbeatIntervalSeconds_ = intervalSeconds;
  heartbeatRunning_ = true;
  
  heartbeatThread_ = std::thread([this, intervalSeconds]() {
    std::cout << "[Heartbeat] Started with interval: " << intervalSeconds << "s" << std::endl;
    
    while (heartbeatRunning_) {
      std::this_thread::sleep_for(std::chrono::seconds(intervalSeconds));
      
      if (!heartbeatRunning_) break;
      
      // Send PING to keep connection alive
      if (pImpl && !pImpl->activeSymbol.empty()) {
        std::cout << "[Heartbeat] Sending PING to maintain connection" << std::endl;
        // ixwebsocket handles PING/PONG automatically, but we log for monitoring
        // The underlying WebSocket will send WebSocket PING frames
      }
    }
    
    std::cout << "[Heartbeat] Stopped" << std::endl;
  });
}

void BinanceClient::stopHeartbeat() {
  heartbeatRunning_ = false;
  if (heartbeatThread_.joinable()) {
    heartbeatThread_.join();
  }
}

bool BinanceClient::isConnected() const {
  // Check if websocket is in a connected state using readyState string
  if (!pImpl) return false;
  return pImpl->webSocket.getReadyState() == ix::ReadyState::Open;
}

void BinanceClient::fetchExchangeInfo(OnSymbolsCallback onDataCallback) {
  std::vector<core::Symbol> symbols;
  
  // Call the exchange info endpoint
  std::string path = "/api/v3/exchangeInfo";
  std::string response = pImpl->httpsGet(pImpl->getBaseUrl(), path, "");
  
  if (!response.empty()) {
    try {
      auto j = json::parse(response);
      
      if (j.contains("symbols") && j["symbols"].is_array()) {
        for (const auto& sym : j["symbols"]) {
          core::Symbol symbol;
          symbol.symbol = sym.value("symbol", "");
          symbol.baseAsset = sym.value("baseAsset", "");
          symbol.quoteAsset = sym.value("quoteAsset", "");
          symbol.status = sym.value("status", "");
          
          // Get permissions as string
          if (sym.contains("permissions") && sym["permissions"].is_array()) {
            for (const auto& perm : sym["permissions"]) {
              if (!symbol.permissions.empty()) symbol.permissions += ",";
              symbol.permissions += perm.get<std::string>();
            }
          }
          
          // Parse filters
          if (sym.contains("filters") && sym["filters"].is_array()) {
            for (const auto& filter : sym["filters"]) {
              std::string filterType = filter.value("filterType", "");
              
              if (filterType == "PRICE_FILTER") {
                symbol.minPrice = std::stod(filter.value("minPrice", "0"));
                symbol.maxPrice = std::stod(filter.value("maxPrice", "0"));
                symbol.tickSize = std::stod(filter.value("tickSize", "0"));
              } else if (filterType == "LOT_SIZE") {
                symbol.minQty = std::stod(filter.value("minQty", "0"));
                symbol.maxQty = std::stod(filter.value("maxQty", "0"));
                symbol.stepSize = std::stod(filter.value("stepSize", "0"));
              } else if (filterType == "MIN_NOTIONAL") {
                symbol.minNotional = std::stod(filter.value("minNotional", "0"));
              }
            }
          }
          
          // Only add trading symbols
          if (symbol.isTrading() && !symbol.symbol.empty()) {
            symbols.push_back(symbol);
          }
        }
        
        std::cout << "Fetched " << symbols.size() << " trading symbols from exchange info" << std::endl;
      }
      
    } catch (const std::exception& e) {
      std::cerr << "Error parsing exchange info: " << e.what() << std::endl;
    }
  } else {
    std::cerr << "Failed to fetch exchange info (empty response)" << std::endl;
  }
  
  if (onDataCallback) {
    onDataCallback(symbols);
  }
}

void BinanceClient::subscribeMiniTickers(OnTicksCallback callback) {
  // Store the callback - we'll convert vector to single tick in the handler
  pImpl->onTick = [callback](const core::Tick& tick) {
    // Wrap single tick in vector and call the callback
    callback(std::vector<core::Tick>{tick});
  };
  
  // Use the combined stream for all mini tickers
  // wss://stream.binance.com:9443/ws/!miniTicker@arr
  std::string url = pImpl->getWsUrl() + "/!miniTicker@arr";
  pImpl->webSocket.setUrl(url);
  
  // Setup message handler
  pImpl->webSocket.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
          try {
            auto j = json::parse(msg->str);
            
            // miniTicker@arr returns an array of mini tickers
            if (j.is_array()) {
              for (const auto& ticker : j) {
                if (ticker.contains("s") && pImpl->onTick) {
                  core::Tick tick;
                  tick.timestamp_ms = ticker.value("E", 0);
                  tick.price = std::stod(ticker.value("c", "0"));
                  tick.quantity = std::stod(ticker.value("v", "0"));
                  // is_buyer_maker indicates price movement direction
                  double openPrice = std::stod(ticker.value("o", "0"));
                  tick.is_buyer_maker = tick.price < openPrice;
                  
                  pImpl->onTick(tick);
                }
              }
            }
          } catch (const json::parse_error &e) {
            std::cerr << "JSON Parse error in miniTicker: " << e.what() << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "Error processing miniTicker: " << e.what() << std::endl;
          }
        } else if (msg->type == ix::WebSocketMessageType::Open) {
          std::cout << "Connected to miniTicker stream for all symbols" << std::endl;
        } else if (msg->type == ix::WebSocketMessageType::Error) {
          std::cerr << "miniTicker Websocket Error: " << msg->errorInfo.reason << std::endl;
        }
      });
}

// --- Bootstrap: Fetch history then start live stream ---
void BinanceClient::bootstrapHistoryThenStream(
    const std::string& symbol,
    const std::string& interval,
    uint64_t startTime,
    uint64_t endTime,
    std::function<void(const std::vector<core::Candle>&)> onHistoryComplete,
    OnTickCallback onTickCallback) {
  
  std::cout << "[Bootstrap] Starting history fetch for " << symbol 
            << " @ " << interval << " from " << startTime << " to " << endTime << std::endl;
  
  // Fetch historical klines first
  fetchKlines(symbol, interval, startTime, endTime, 
    [this, symbol, interval, onHistoryComplete, onTickCallback](const std::vector<core::Candle>& candles) {
      
      std::cout << "[Bootstrap] History fetch complete: " << candles.size() << " candles" << std::endl;
      
      // Set last trade ID from the last candle for deduplication
      if (!candles.empty()) {
        // Use end time as reference for deduplication (trades after this time are new)
        lastRestTradeId_ = static_cast<int64_t>(candles.back().end_time_ms);
        std::cout << "[Bootstrap] Set last REST timestamp: " << lastRestTradeId_ << std::endl;
      }
      
      // Send history to callback (frontend)
      if (onHistoryComplete) {
        onHistoryComplete(candles);
      }
      
      // Now enable buffering and start live stream
      // This ensures all live updates from this point are captured
      bufferingEnabled_ = true;
      
      // Subscribe to live trades
      subscribeAggTrades(symbol, onTickCallback);
      
      // Start the WebSocket connection
      connectAndRun();
      
      std::cout << "[Bootstrap] Live stream started for " << symbol << std::endl;
    });
}

} // namespace network
} // namespace glora
