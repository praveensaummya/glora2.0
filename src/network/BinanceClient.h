#pragma once

#include "../core/DataModels.h"
#include "../settings/Settings.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>
#include <queue>
#include <unordered_set>
#include <mutex>
#include <chrono>
#include <thread>

namespace glora {
namespace network {

using json = nlohmann::json;

// Callback signatures
using OnCandleCallback = std::function<void(const core::Candle &)>;
using OnTickCallback = std::function<void(const core::Tick &)>;
using OnTicksCallback = std::function<void(const std::vector<core::Tick> &)>;
using OnDepthCallback = std::function<void(const std::vector<std::pair<double, double>>& bids, const std::vector<std::pair<double, double>>& asks)>;
using OnSymbolsCallback = std::function<void(const std::vector<core::Symbol> &)>;

class BinanceClient {
public:
  BinanceClient();
  ~BinanceClient();

  // Initialize with user API configuration
  bool initialize(const settings::ApiConfig* config = nullptr);

  // Update API configuration
  void setApiConfig(const settings::ApiConfig& config);

  // --- REST API ---
  // Fetch historical aggregated trades for footprint generation
  void fetchHistoricalAggTrades(
      const std::string &symbol, uint64_t startTime, uint64_t endTime,
      std::function<void(const std::vector<core::Tick> &)> onDataCallback);

  // Fetch klines (candlesticks)
  void fetchKlines(const std::string& symbol, const std::string& interval,
                    uint64_t startTime, uint64_t endTime,
                    std::function<void(const std::vector<core::Candle>&)> onDataCallback);

  // Fetch order book depth (for DOM display)
  void fetchDepth(const std::string& symbol, int limit,
                  std::function<void(const std::vector<std::pair<double, double>>& bids,
                                    const std::vector<std::pair<double, double>>& asks)> onDataCallback);

  // Fetch exchange info (symbol metadata)
  void fetchExchangeInfo(OnSymbolsCallback onDataCallback);

  // --- WebSockets ---
  // Subscribe to real-time aggTrade stream
  void subscribeAggTrades(const std::string &symbol, OnTickCallback callback);

  // Subscribe to depth stream (order book updates)
  void subscribeDepth(const std::string& symbol, OnDepthCallback callback);

  // Subscribe to miniTicker for all symbols (real-time price updates)
  void subscribeMiniTickers(OnTicksCallback callback);

  // Connect and start the ASIO event loop on the network thread
  void connectAndRun();

  // Shutdown cleanly
  void shutdown();

  // Check if using user API
  bool hasApiCredentials() const { return hasApiConfig_; }

  // --- Bootstrap: Fetch history then start live stream ---
  // This ensures historical data is loaded before live updates begin
  void bootstrapHistoryThenStream(
      const std::string& symbol,
      const std::string& interval,
      uint64_t startTime,
      uint64_t endTime,
      std::function<void(const std::vector<core::Candle>&)> onHistoryComplete,
      OnTickCallback onTickCallback);

  // --- Race Condition Fix: WebSocket Buffering & ID-based Deduplication ---
  // Enable buffering of WebSocket messages before REST fetch completes
  void enableBuffering(bool enable) { bufferingEnabled_ = enable; }
  
  // Flush buffered messages after REST fetch completes
  void flushBuffer();
  
  // Set the last trade ID from REST fetch for deduplication
  void setLastTradeId(int64_t lastId) { lastRestTradeId_ = lastId; }
  
  // Get buffered message count
  size_t getBufferSize() const;

  // --- Rate Limit Fix: PING/PONG Heartbeat ---
  // Start heartbeat with specified interval (default 20 seconds)
  void startHeartbeat(uint32_t intervalSeconds = 20);
  
  // Stop heartbeat
  void stopHeartbeat();
  
  // Check if connection is alive
  bool isConnected() const;

private:
  // Internal state (Boost Asio contexts, Websocket streams, etc) would go here
  struct Impl;
  std::unique_ptr<Impl> pImpl;
  bool hasApiConfig_ = false;
  
  // --- Race Condition Fix Members ---
  bool bufferingEnabled_ = false;
  std::queue<json> wsMessageBuffer_;
  mutable std::mutex bufferMutex_;
  std::unordered_set<int64_t> seenTradeIds_;
  int64_t lastRestTradeId_ = 0;
  
  // --- Rate Limit Fix Members ---
  bool heartbeatRunning_ = false;
  uint32_t heartbeatIntervalSeconds_ = 20;
  std::thread heartbeatThread_;
};

} // namespace network
} // namespace glora
