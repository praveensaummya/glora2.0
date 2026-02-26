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

  // Binance websocket format for aggTrades is
  // wss://stream.binance.com:9443/ws/<symbol>@aggTrade
  std::string lowerSymbol = symbol;
  for (auto &c : lowerSymbol)
    c = std::tolower(c);

  std::string url = pImpl->getWsUrl() + "/" + lowerSymbol + "@aggTrade";
  pImpl->webSocket.setUrl(url);

  // Setup message handler
  pImpl->webSocket.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
          try {
            auto j = json::parse(msg->str);

            if (j.contains("e") && j["e"] == "aggTrade") {
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
  }
}

void BinanceClient::shutdown() {
  std::cout << "Shutting down Binance Client..." << std::endl;
  pImpl->webSocket.stop();
  ix::uninitNetSystem();
}

} // namespace network
} // namespace glora
