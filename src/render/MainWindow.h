#pragma once

#include "../core/DataModels.h"
#include "ChartInteractionHandler.h"
#include "WebViewManager.h"
#include "../network/WebSocketServer.h"
#include <memory>
#include <string>

namespace glora {
namespace render {

class MainWindow {
public:
  MainWindow(int width, int height, const std::string &title);
  ~MainWindow();

  // Initialize display and graphics context
  bool initialize();

  // Start the rendering loop
  void run();

  // Update the chart with new data from the network thread
  void updateSymbolData(const core::SymbolData &data);

  // Add a single raw tick for testing
  void addRawTick(const core::Tick &tick);

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
  
  // Handle IPC messages from React frontend
  void handleIPCMessage(const std::string& jsonMessage);
  
  // Send candle data to React frontend
  void sendCandleToFrontend(const core::Candle& candle, const std::string& symbol);
  
  // Subscribe to market data
  void subscribeToMarketData(const std::string& symbol, const std::string& interval);
  
  // Unsubscribe from market data
  void unsubscribeFromMarketData();
};

} // namespace render
} // namespace glora
