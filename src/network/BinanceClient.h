#pragma once

#include "../core/DataModels.h"
#include "../settings/Settings.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace glora {
namespace network {

using json = nlohmann::json;

// Callback signatures
using OnCandleCallback = std::function<void(const core::Candle &)>;
using OnTickCallback = std::function<void(const core::Tick &)>;
using OnTicksCallback = std::function<void(const std::vector<core::Tick> &)>;
using OnDepthCallback = std::function<void(const std::vector<std::pair<double, double>>& bids, const std::vector<std::pair<double, double>>& asks)>;

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

  // --- WebSockets ---
  // Subscribe to real-time aggTrade stream
  void subscribeAggTrades(const std::string &symbol, OnTickCallback callback);

  // Connect and start the ASIO event loop on the network thread
  void connectAndRun();

  // Shutdown cleanly
  void shutdown();

  // Check if using user API
  bool hasApiCredentials() const { return hasApiConfig_; }

private:
  // Internal state (Boost Asio contexts, Websocket streams, etc) would go here
  struct Impl;
  std::unique_ptr<Impl> pImpl;
  bool hasApiConfig_ = false;
};

} // namespace network
} // namespace glora
