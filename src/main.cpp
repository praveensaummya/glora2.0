#include <iostream>
#include <thread>

#include "core/DataModels.h"
#include "core/ThreadSafeQueue.h"
#include "network/BinanceClient.h"
#include "render/MainWindow.h"

int main(int argc, char *argv[]) {
  std::cout << "Starting Glora Charting App..." << std::endl;

  // 1. Initialize Network Client
  glora::network::BinanceClient binanceClient;
  if (!binanceClient.initialize()) {
    std::cerr << "Failed to initialize Binance Client" << std::endl;
    return 1;
  }

  // 2. Initialize UI / Render Engine
  glora::render::MainWindow mainWindow(1280, 720, "Glora Charting - BTCUSDT");
  if (!mainWindow.initialize()) {
    std::cerr << "Failed to initialize MainWindow" << std::endl;
    return 1;
  }

  // 3. Setup communication queue between Network and UI
  glora::core::ThreadSafeQueue<glora::core::Tick> tickQueue;

  // 4. Start Network Thread
  std::thread networkThread([&]() {
    binanceClient.subscribeAggTrades(
        "BTCUSDT",
        [&](const glora::core::Tick &tick) { tickQueue.push(tick); });
    binanceClient.connectAndRun();
  });

  std::cout << "Starting UI Loop..." << std::endl;
  // 5. Consume ticks and print them (Simulating the UI loop)
  // We launch another thread to decouple network queue processing from the UI
  // thread latency
  std::thread processingThread([&]() {
    while (true) {
      auto tickOpt = tickQueue.pop(); // This blocks until a tick is available
      if (tickOpt.has_value()) {
        mainWindow.addRawTick(tickOpt.value());
      } else {
        break;
      }
    }
  });

  mainWindow.run(); // This blocks the main thread

  // Shutdown signals
  tickQueue.invalidate();

  // 6. Shutdown
  binanceClient.shutdown();

  if (processingThread.joinable()) {
    processingThread.join();
  }
  if (networkThread.joinable()) {
    networkThread.join();
  }

  std::cout << "Exiting correctly." << std::endl;
  return 0;
}
