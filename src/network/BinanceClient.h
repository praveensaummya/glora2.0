#pragma once

#include "../core/DataModels.h"
#include <functional>
#include <memory>
#include <nlohmann/json.hpp>
#include <string>

namespace glora {
namespace network {

using json = nlohmann::json;

// Callback signatures
using OnCandleCallback = std::function<void(const core::Candle &)>;
using OnTickCallback = std::function<void(const core::Tick &)>;

class BinanceClient {
public:
  BinanceClient();
  ~BinanceClient();

  // Initialize connection pools, DNS resolution, etc
  bool initialize();

  // --- REST API ---
  // Fetch historical aggregated trades for footprint generation
  void fetchHistoricalAggTrades(
      const std::string &symbol, uint64_t startTime, uint64_t endTime,
      std::function<void(const std::vector<core::Tick> &)> onDataCallback);

  // --- WebSockets ---
  // Subscribe to real-time aggTrade stream
  void subscribeAggTrades(const std::string &symbol, OnTickCallback callback);

  // Connect and start the ASIO event loop on the network thread
  void connectAndRun();

  // Shutdown cleanly
  void shutdown();

private:
  // Internal state (Boost Asio contexts, Websocket streams, etc) would go here
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace network
} // namespace glora
