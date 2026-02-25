#pragma once

#include "../core/DataManager.h"
#include "../database/Database.h"
#include "../network/BinanceClient.h"
#include "../network/WebSocketServer.h"
#include "../settings/Settings.h"
#include <memory>
#include <string>
#include <functional>
#include <nlohmann/json.hpp>

namespace glora {
namespace network {

using json = nlohmann::json;

/**
 * ApiHandler - Handles incoming messages from frontend via WebSocket
 * 
 * Message Protocol:
 * - "getHistory": Fetch historical candles (with days or startTime/endTime)
 * - "getFootprint": Get footprint data for specific candle
 * - "subscribe": Subscribe to real-time updates for a symbol
 * - "setConfig": Configure data fetch parameters (days: 5-7)
 * - "getStatus": Get current backend status
 */
class ApiHandler {
public:
    ApiHandler();
    ~ApiHandler();

    /**
     * Initialize the API handler with required dependencies
     */
    bool initialize(
        std::shared_ptr<core::DataManager> dataManager,
        std::shared_ptr<database::Database> database,
        std::shared_ptr<BinanceClient> binanceClient,
        std::shared_ptr<WebSocketServer> wsServer,
        const settings::AppSettings& settings
    );

    /**
     * Process incoming message from frontend
     * @param message JSON message string from frontend
     */
    void handleMessage(const std::string& message);

    /**
     * Send a message to all connected frontend clients
     */
    void broadcast(const json& message);

    /**
     * Set callback for real-time tick data
     */
    void setOnTickCallback(std::function<void(const core::Tick&)> callback);

    /**
     * Set callback for quit request
     */
    void setOnQuitCallback(std::function<void()> callback);

    /**
     * Get current settings
     */
    const settings::AppSettings& getSettings() const { return settings_; }

    /**
     * Update settings
     */
    void updateSettings(const settings::AppSettings& settings);

private:
    // Message handlers
    void handleGetHistory(const json& message);
    void handleGetFootprint(const json& message);
    void handleSubscribe(const json& message);
    void handleSetConfig(const json& message);
    void handleGetStatus(const json& message);
    void handleGetTicks(const json& message);
    void handleSaveCredentials(const json& message);
    void handleLoadCredentials(const json& message);
    void handleDeleteCredentials(const json& message);

    // Response builders
    json buildHistoryResponse(const std::vector<core::Candle>& candles);
    json buildFootprintResponse(const core::Candle& candle);
    json buildErrorResponse(const std::string& error);
    json buildStatusResponse();

    // Helper to get response destination
    std::string getRequestId(const json& message);
    std::string getSymbol(const json& message);

    // Dependencies
    std::shared_ptr<core::DataManager> dataManager_;
    std::shared_ptr<database::Database> database_;
    std::shared_ptr<BinanceClient> binanceClient_;
    std::shared_ptr<WebSocketServer> wsServer_;
    settings::AppSettings settings_;

    // State
    bool isInitialized_ = false;
    std::string currentSymbol_;
    std::function<void(const core::Tick&)> onTickCallback_;
    std::function<void()> onQuitCallback_;
};

} // namespace network
} // namespace glora
