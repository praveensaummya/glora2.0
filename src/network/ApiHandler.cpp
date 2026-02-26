#include "ApiHandler.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace glora {
namespace network {

ApiHandler::ApiHandler() {}

ApiHandler::~ApiHandler() {}

bool ApiHandler::initialize(
    std::shared_ptr<core::DataManager> dataManager,
    std::shared_ptr<database::Database> database,
    std::shared_ptr<BinanceClient> binanceClient,
    std::shared_ptr<WebSocketServer> wsServer,
    const settings::AppSettings& settings) {
    
    dataManager_ = dataManager;
    database_ = database;
    binanceClient_ = binanceClient;
    wsServer_ = wsServer;
    settings_ = settings;
    currentSymbol_ = settings.defaultSymbol;
    
    // Set up message callback on WebSocketServer
    if (wsServer_) {
        wsServer_->setMessageCallback([this](const std::string& message) {
            this->handleMessage(message);
        });
    }
    
    // Initialize DataManager
    if (dataManager_) {
        dataManager_->initialize(settings_);
        dataManager_->setNetworkClient(binanceClient_);
        dataManager_->setDatabase(database_);
    }
    
    isInitialized_ = true;
    std::cout << "[ApiHandler] Initialized successfully" << std::endl;
    return true;
}

void ApiHandler::handleMessage(const std::string& messageStr) {
    if (!isInitialized_) {
        std::cerr << "[ApiHandler] Not initialized, ignoring message" << std::endl;
        return;
    }
    
    try {
        json message = json::parse(messageStr);
        std::string type = message.value("type", "");
        
        std::cout << "[ApiHandler] Received message type: " << type << std::endl;
        
        if (type == "getHistory") {
            handleGetHistory(message);
        } else if (type == "getFootprint") {
            handleGetFootprint(message);
        } else if (type == "subscribe") {
            handleSubscribe(message);
        } else if (type == "setConfig") {
            handleSetConfig(message);
        } else if (type == "getStatus") {
            handleGetStatus(message);
        } else if (type == "getTicks") {
            handleGetTicks(message);
        } else if (type == "saveCredentials") {
            handleSaveCredentials(message);
        } else if (type == "loadCredentials") {
            handleLoadCredentials(message);
        } else if (type == "deleteCredentials") {
            handleDeleteCredentials(message);
        } else if (type == "quit") {
            std::cout << "[ApiHandler] Quit requested" << std::endl;
            // Signal shutdown - we'll handle this via callback
            if (onQuitCallback_) {
                onQuitCallback_();
            }
        } else {
            std::cerr << "[ApiHandler] Unknown message type: " << type << std::endl;
            auto response = buildErrorResponse("Unknown message type: " + type);
            broadcast(response);
        }
    } catch (const json::parse_error& e) {
        std::cerr << "[ApiHandler] JSON parse error: " << e.what() << std::endl;
        auto response = buildErrorResponse("Invalid JSON: " + std::string(e.what()));
        broadcast(response);
    } catch (const std::exception& e) {
        std::cerr << "[ApiHandler] Error handling message: " << e.what() << std::endl;
        auto response = buildErrorResponse(std::string(e.what()));
        broadcast(response);
    }
}

void ApiHandler::handleGetHistory(const json& message) {
    std::string symbol = message.value("symbol", currentSymbol_);
    int days = message.value("days", 7); // Default 7 days
    
    // Clamp days between 1 and 30
    days = std::max(1, std::min(days, 30));
    
    // Or use custom time range if provided
    uint64_t startTime = 0;
    uint64_t endTime = 0;
    
    if (message.contains("startTime") && message.contains("endTime")) {
        startTime = message["startTime"].get<uint64_t>();
        endTime = message["endTime"].get<uint64_t>();
    } else {
        // Calculate from days
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        endTime = now;
        startTime = now - (static_cast<uint64_t>(days) * 24 * 60 * 60 * 1000);
    }
    
    std::string interval = message.value("interval", "1m");
    
    std::cout << "[ApiHandler] Fetching history for " << symbol 
              << " from " << startTime << " to " << endTime 
              << " (interval: " << interval << ", days: " << days << ")" << std::endl;
    
    // Check if interval changed
    bool intervalChanged = (interval != currentInterval_);
    
    // Check if we need to fetch from API (no data, or interval changed)
    bool needsFetch = false;
    
    // Fetch data from database first
    std::vector<core::Candle> candles;
    if (database_ && !intervalChanged) {
        candles = database_->getCandles(symbol, startTime, endTime);
        std::cout << "[ApiHandler] Found " << candles.size() << " candles in database" << std::endl;
    }
    
    // If interval changed or no data/insufficient data, fetch from API
    if (intervalChanged) {
        std::cout << "[ApiHandler] Interval changed from " << currentInterval_ << " to " << interval << ", fetching from API" << std::endl;
        currentInterval_ = interval;
        needsFetch = true;
    } else if (candles.empty() || 
        (candles.front().start_time_ms > startTime + 60000)) {
        needsFetch = true;
    }
    
    if (needsFetch) {
        // Missing data at the beginning, fetch from API
        if (binanceClient_) {
            std::cout << "[ApiHandler] Fetching missing data from Binance..." << std::endl;
            binanceClient_->fetchKlines(
                symbol,
                interval,
                startTime,
                endTime,
                [this, symbol, message, interval](const std::vector<core::Candle>& fetchedCandles) {
                    std::cout << "[ApiHandler] Fetched " << fetchedCandles.size() 
                              << " candles for interval " << interval << " from Binance" << std::endl;
                    if (!fetchedCandles.empty() && database_) {
                        database_->insertCandles(symbol, fetchedCandles);
                        std::cout << "[ApiHandler] Saved " << fetchedCandles.size() 
                                  << " candles to database" << std::endl;
                    }
                    // Use the fetched candles directly instead of re-querying database
                    // (database doesn't filter by interval, so re-querying would return wrong data)
                    auto response = buildHistoryResponse(fetchedCandles);
                    response["interval"] = interval;
                    response["requestId"] = getRequestId(message);
                    broadcast(response);
                }
            );
            return; // Don't send response now - wait for async callback
        }
    }
    
    // Return cached candles
    auto response = buildHistoryResponse(candles);
    response["requestId"] = getRequestId(message);
    broadcast(response);
}

void ApiHandler::handleGetFootprint(const json& message) {
    std::string symbol = message.value("symbol", currentSymbol_);
    uint64_t candleTime = message.value("candleTime", 0);
    
    if (candleTime == 0) {
        auto response = buildErrorResponse("Missing candleTime parameter");
        broadcast(response);
        return;
    }
    
    std::cout << "[ApiHandler] Getting footprint for " << symbol 
              << " at time " << candleTime << std::endl;
    
    // Get candles from database
    if (database_) {
        uint64_t startTime = candleTime;
        uint64_t endTime = candleTime + 60000; // 1 minute
        auto candles = database_->getCandles(symbol, startTime, endTime);
        
        if (!candles.empty()) {
            auto response = buildFootprintResponse(candles.front());
            response["requestId"] = getRequestId(message);
            broadcast(response);
        } else {
            auto response = buildErrorResponse("No candle found at specified time");
            broadcast(response);
        }
    }
}

void ApiHandler::handleSubscribe(const json& message) {
    std::string symbol = message.value("symbol", currentSymbol_);
    currentSymbol_ = symbol;
    
    std::cout << "[ApiHandler] Subscribing to " << symbol << std::endl;
    
    // Subscribe to real-time updates
    if (binanceClient_) {
        binanceClient_->subscribeAggTrades(
            symbol,
            [this](const core::Tick& tick) {
                // Broadcast tick to all clients
                json tickMsg = {
                    {"type", "tick"},
                    {"symbol", currentSymbol_},
                    {"time", tick.timestamp_ms},
                    {"price", tick.price},
                    {"quantity", tick.quantity},
                    {"isBuyerMaker", tick.is_buyer_maker}
                };
                broadcast(tickMsg);
                
                // Also pass to DataManager
                if (dataManager_) {
                    dataManager_->addLiveTick(tick);
                }
                
                // Call external callback if set
                if (onTickCallback_) {
                    onTickCallback_(tick);
                }
            }
        );
        binanceClient_->connectAndRun();
    }
    
    // Send confirmation
    json response = {
        {"type", "subscribed"},
        {"symbol", symbol},
        {"status", "ok"}
    };
    response["requestId"] = getRequestId(message);
    broadcast(response);
}

void ApiHandler::handleSetConfig(const json& message) {
    if (message.contains("days")) {
        int days = message["days"].get<int>();
        days = std::max(1, std::min(days, 30)); // Clamp 1-30
        
        if (settings_.historyDuration == settings::HistoryDuration::CUSTOM) {
            settings_.customDays = days;
        } else {
            settings_.historyDuration = settings::HistoryDuration::CUSTOM;
            settings_.customDays = days;
        }
        
        std::cout << "[ApiHandler] Config updated: days = " << days << std::endl;
    }
    
    if (message.contains("symbol")) {
        currentSymbol_ = message["symbol"].get<std::string>();
    }
    
    if (message.contains("interval")) {
        settings_.defaultInterval = message["interval"].get<std::string>();
    }
    
    // Update DataManager settings
    if (dataManager_) {
        dataManager_->initialize(settings_);
    }
    
    // Send confirmation
    json response = {
        {"type", "config"},
        {"status", "ok"},
        {"days", settings_.historyDuration == settings::HistoryDuration::CUSTOM ? 
                 settings_.customDays : 7}
    };
    response["requestId"] = getRequestId(message);
    broadcast(response);
}

void ApiHandler::handleGetStatus(const json& message) {
    auto response = buildStatusResponse();
    response["requestId"] = getRequestId(message);
    broadcast(response);
}

void ApiHandler::handleGetTicks(const json& message) {
    std::string symbol = message.value("symbol", currentSymbol_);
    uint64_t startTime = message.value("startTime", 0);
    uint64_t endTime = message.value("endTime", 0);
    
    if (startTime == 0 || endTime == 0) {
        // Default to last hour
        uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        endTime = now;
        startTime = now - (60 * 60 * 1000); // 1 hour
    }
    
    std::cout << "[ApiHandler] Getting ticks for " << symbol 
              << " from " << startTime << " to " << endTime << std::endl;
    
    std::vector<core::Tick> ticks;
    if (database_) {
        ticks = database_->getTicks(symbol, startTime, endTime);
    }
    
    json response = {
        {"type", "ticks"},
        {"symbol", symbol},
        {"count", ticks.size()}
    };
    
    // Add tick data
    json tickArray = json::array();
    for (const auto& tick : ticks) {
        tickArray.push_back({
            {"t", tick.timestamp_ms},
            {"p", tick.price},
            {"q", tick.quantity},
            {"m", tick.is_buyer_maker}
        });
    }
    response["ticks"] = tickArray;
    response["requestId"] = getRequestId(message);
    broadcast(response);
}

void ApiHandler::broadcast(const json& message) {
    if (wsServer_ && wsServer_->isRunning()) {
        wsServer_->broadcast(message);
    }
}

void ApiHandler::setOnTickCallback(std::function<void(const core::Tick&)> callback) {
    onTickCallback_ = std::move(callback);
}

void ApiHandler::setOnQuitCallback(std::function<void()> callback) {
    onQuitCallback_ = std::move(callback);
}

void ApiHandler::updateSettings(const settings::AppSettings& settings) {
    settings_ = settings;
    if (dataManager_) {
        dataManager_->initialize(settings_);
    }
}

json ApiHandler::buildHistoryResponse(const std::vector<core::Candle>& candles) {
    json response = {
        {"type", "history"},
        {"symbol", currentSymbol_},
        {"count", candles.size()}
    };
    
    json candleArray = json::array();
    for (const auto& candle : candles) {
        json c = {
            {"time", candle.start_time_ms},
            {"open", candle.open},
            {"high", candle.high},
            {"low", candle.low},
            {"close", candle.close},
            {"volume", candle.volume}
        };
        
        // Include footprint if available
        if (!candle.footprint_profile.empty()) {
            json footprint = json::object();
            for (const auto& [price, node] : candle.footprint_profile) {
                footprint[std::to_string(price)] = {
                    {"bid", node.bid_volume},
                    {"ask", node.ask_volume}
                };
            }
            c["footprint"] = footprint;
        }
        
        candleArray.push_back(c);
    }
    
    response["candles"] = candleArray;
    return response;
}

json ApiHandler::buildFootprintResponse(const core::Candle& candle) {
    json response = {
        {"type", "footprint"},
        {"symbol", currentSymbol_},
        {"time", candle.start_time_ms},
        {"open", candle.open},
        {"high", candle.high},
        {"low", candle.low},
        {"close", candle.close},
        {"volume", candle.volume}
    };
    
    json footprint = json::object();
    for (const auto& [price, node] : candle.footprint_profile) {
        footprint[std::to_string(price)] = {
            {"bid", node.bid_volume},
            {"ask", node.ask_volume},
            {"delta", node.ask_volume - node.bid_volume}
        };
    }
    response["profile"] = footprint;
    
    return response;
}

json ApiHandler::buildErrorResponse(const std::string& error) {
    return {
        {"type", "error"},
        {"error", error}
    };
}

json ApiHandler::buildStatusResponse() {
    uint64_t dbTicks = 0;
    uint64_t dbCandles = 0;
    
    if (database_) {
        auto latestTick = database_->getLatestTickTime(currentSymbol_);
        auto earliestTick = database_->getEarliestTickTime(currentSymbol_);
        
        if (latestTick.has_value()) {
            dbTicks = latestTick.value();
        }
    }
    
    return {
        {"type", "status"},
        {"symbol", currentSymbol_},
        {"connected", binanceClient_ != nullptr},
        {"database", database_ != nullptr},
        {"latestTick", dbTicks},
        {"historyDays", settings_.historyDuration == settings::HistoryDuration::CUSTOM ? 
                       settings_.customDays : 7}
    };
}

std::string ApiHandler::getRequestId(const json& message) {
    if (message.contains("id")) {
        return std::to_string(message["id"].get<int>());
    }
    if (message.contains("requestId")) {
        return std::to_string(message["requestId"].get<int>());
    }
    return "";
}

std::string ApiHandler::getSymbol(const json& message) {
    return message.value("symbol", currentSymbol_);
}

void ApiHandler::handleSaveCredentials(const json& message) {
    std::string apiKey = message.value("apiKey", "");
    std::string apiSecret = message.value("apiSecret", "");
    bool useTestnet = message.value("useTestnet", false);
    
    if (apiKey.empty() || apiSecret.empty()) {
        auto response = buildErrorResponse("API key and secret are required");
        broadcast(response);
        return;
    }
    
    std::cout << "[ApiHandler] Saving API credentials..." << std::endl;
    
    if (database_) {
        bool success = database_->saveApiCredentials(apiKey, apiSecret, useTestnet);
        
        if (success) {
            // Update the Binance client with new credentials
            settings::ApiConfig config;
            config.apiKey = apiKey;
            config.apiSecret = apiSecret;
            config.useTestnet = useTestnet;
            
            if (binanceClient_) {
                binanceClient_->setApiConfig(config);
            }
            
            json response = {
                {"type", "credentialsSaved"},
                {"status", "ok"},
                {"message", "API credentials saved successfully"}
            };
            response["requestId"] = getRequestId(message);
            broadcast(response);
        } else {
            auto response = buildErrorResponse("Failed to save credentials");
            broadcast(response);
        }
    }
}

void ApiHandler::handleLoadCredentials(const json& message) {
    std::cout << "[ApiHandler] Loading API credentials..." << std::endl;
    
    if (database_) {
        std::string apiKey, apiSecret;
        bool useTestnet;
        
        bool hasCredentials = database_->getApiCredentials(apiKey, apiSecret, useTestnet);
        
        json response = {
            {"type", "credentialsLoaded"},
            {"hasCredentials", hasCredentials}
        };
        
        if (hasCredentials) {
            // Mask the API secret for security
            std::string maskedSecret = apiSecret.length() > 4 
                ? std::string(apiSecret.length() - 4, '*') + apiSecret.substr(apiSecret.length() - 4)
                : "****";
            
            response["apiKey"] = apiKey;
            response["apiSecret"] = maskedSecret;
            response["useTestnet"] = useTestnet;
            
            // Also apply to Binance client
            settings::ApiConfig config;
            config.apiKey = apiKey;
            config.apiSecret = apiSecret;
            config.useTestnet = useTestnet;
            
            if (binanceClient_) {
                binanceClient_->setApiConfig(config);
            }
        }
        
        response["requestId"] = getRequestId(message);
        broadcast(response);
    }
}

void ApiHandler::handleDeleteCredentials(const json& message) {
    std::cout << "[ApiHandler] Deleting API credentials..." << std::endl;
    
    if (database_) {
        bool success = database_->deleteApiCredentials();
        
        json response = {
            {"type", "credentialsDeleted"},
            {"status", success ? "ok" : "error"}
        };
        response["requestId"] = getRequestId(message);
        broadcast(response);
    }
}

} // namespace network
} // namespace glora
