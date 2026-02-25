#include <iostream>
#include <thread>
#include <chrono>
#include <memory>
#include <atomic>
#include <csignal>

#include "core/DataModels.h"
#include "core/ThreadSafeQueue.h"
#include "database/Database.h"
#include "network/BinanceClient.h"
#include "network/WebSocketServer.h"
#include "network/ApiHandler.h"
#include "settings/Settings.h"
#include "render/MainWindow.h"

using namespace glora::settings;

int main(int argc, char *argv[]) {
  std::cout << "Starting Glora Charting App..." << std::endl;

  // 1. Initialize Settings
  glora::settings::AppSettings settings;
  settings.defaultSymbol = "BTCUSDT";
  settings.defaultInterval = "1m";
  settings.historyDuration = glora::settings::HistoryDuration::LAST_7_DAYS;
  settings.customDays = 7;

  // 2. Initialize Database
  auto database = std::make_shared<glora::database::Database>();
  if (!database->initialize("glora_data.db")) {
    std::cerr << "Failed to initialize Database" << std::endl;
    return 1;
  }
  std::cout << "Database initialized successfully" << std::endl;

  // 3. Initialize Network Client
  auto binanceClient = std::make_shared<glora::network::BinanceClient>();
  if (!binanceClient->initialize()) {
    std::cerr << "Failed to initialize Binance Client" << std::endl;
    return 1;
  }

  // 4. Initialize WebSocket Server for frontend communication
  auto wsServer = std::make_shared<glora::network::WebSocketServer>(8080);
  if (!wsServer->start()) {
    std::cerr << "Failed to start WebSocket Server" << std::endl;
    return 1;
  }
  std::cout << "WebSocket Server started on port 8080" << std::endl;

  // 5. Initialize Data Manager
  auto dataManager = std::make_shared<glora::core::DataManager>();
  dataManager->initialize(settings);
  dataManager->setNetworkClient(binanceClient);
  dataManager->setDatabase(database);
  
  // 5a. Load initial data and detect/fill gaps on startup
  std::cout << "[Main] Loading initial data and detecting gaps..." << std::endl;
  dataManager->loadSymbolData(settings.defaultSymbol);
  
  // 5b. Load saved API credentials from database
  std::string savedApiKey, savedApiSecret;
  bool savedUseTestnet = false;
  if (database && database->getApiCredentials(savedApiKey, savedApiSecret, savedUseTestnet)) {
    std::cout << "[Main] Found saved API credentials, applying to Binance client..." << std::endl;
    glora::settings::ApiConfig config;
    config.apiKey = savedApiKey;
    config.apiSecret = savedApiSecret;
    config.useTestnet = savedUseTestnet;
    binanceClient->setApiConfig(config);
  }

  // 6. Initialize API Handler (connects all components)
  auto apiHandler = std::make_shared<glora::network::ApiHandler>();
  if (!apiHandler->initialize(dataManager, database, binanceClient, wsServer, settings)) {
    std::cerr << "Failed to initialize API Handler" << std::endl;
    return 1;
  }
  std::cout << "API Handler initialized successfully" << std::endl;

  // 7. Initialize UI / Render Engine (optional - for desktop version)
  glora::render::MainWindow mainWindow(1280, 720, "Glora Charting - BTCUSDT");
  if (!mainWindow.initialize()) {
    std::cerr << "Failed to initialize MainWindow" << std::endl;
    // Continue without UI for headless operation
  }

  // 8. Setup communication queue between Network and UI
  glora::core::ThreadSafeQueue<glora::core::Tick> tickQueue;

  // 9. Subscribe to real-time data
  binanceClient->subscribeAggTrades(
      settings.defaultSymbol,
      [&](const glora::core::Tick &tick) { 
        tickQueue.push(tick); 
        
        // Also broadcast to frontend via API Handler
        apiHandler->broadcast(nlohmann::json{
          {"type", "tick"},
          {"symbol", settings.defaultSymbol},
          {"time", tick.timestamp_ms},
          {"price", tick.price},
          {"quantity", tick.quantity},
          {"isBuyerMaker", tick.is_buyer_maker}
        });
      });

  // Set up data update callback to broadcast candle updates
  dataManager->setOnDataUpdateCallback([&apiHandler, &settings, &dataManager]() {
    // Get latest candles and broadcast to frontend
    const auto& candles = dataManager->getCandles(settings.defaultSymbol);
    if (!candles.empty()) {
      const auto& latestCandle = candles.back();
      nlohmann::json candleMsg = nlohmann::json::object();
      candleMsg["type"] = "candle";
      candleMsg["symbol"] = settings.defaultSymbol;
      candleMsg["time"] = latestCandle.start_time_ms;
      candleMsg["open"] = latestCandle.open;
      candleMsg["high"] = latestCandle.high;
      candleMsg["low"] = latestCandle.low;
      candleMsg["close"] = latestCandle.close;
      apiHandler->broadcast(candleMsg);
    }
  });

  // 10. Start Network Thread
  std::thread networkThread([&]() {
    binanceClient->connectAndRun();
  });

  // 11. Start Data Processing Thread
  std::thread processingThread([&]() {
    while (true) {
      auto tickOpt = tickQueue.pop();
      if (tickOpt.has_value()) {
        mainWindow.addRawTick(tickOpt.value());
        dataManager->addLiveTick(tickOpt.value());
      } else {
        break;
      }
    }
  });
  
  // 12. Start Hourly Cleanup Thread (removes data older than 7 days)
  std::thread cleanupThread([&database, &settings]() {
    const int CLEANUP_INTERVAL_HOURS = 1;
    const int KEEP_DAYS = 7;
    
    while (true) {
      std::this_thread::sleep_for(std::chrono::hours(CLEANUP_INTERVAL_HOURS));
      
      std::cout << "[Main] Running hourly data cleanup..." << std::endl;
      if (database) {
        database->cleanupOldData(KEEP_DAYS);
      }
    }
  });

  std::cout << "Application running. Frontend should connect to ws://localhost:8080" << std::endl;
  std::cout << "Press 'Q' or 'q' to quit" << std::endl;
  std::cout << "API endpoints available:" << std::endl;
  std::cout << "  - getHistory: { type: 'getHistory', symbol: 'BTCUSDT', days: 7 }" << std::endl;
  std::cout << "  - getFootprint: { type: 'getFootprint', symbol: 'BTCUSDT', candleTime: <timestamp> }" << std::endl;
  std::cout << "  - subscribe: { type: 'subscribe', symbol: 'BTCUSDT' }" << std::endl;
  std::cout << "  - setConfig: { type: 'setConfig', days: 5 }" << std::endl;
  std::cout << "  - quit: { type: 'quit' } (or press Q in console)" << std::endl;

  // Add quit message handler to API Handler
  std::atomic<bool> quitRequested(false);
  apiHandler->setOnQuitCallback([&quitRequested]() {
    std::cout << "[Main] Quit requested via API" << std::endl;
    quitRequested = true;
  });

  // Console input listener thread for 'q' or 'quit' command
  std::thread consoleInputThread([&quitRequested]() {
    std::string input;
    while (!quitRequested.load()) {
      if (std::getline(std::cin, input)) {
        if (input == "q" || input == "Q" || input == "quit" || input == "QUIT") {
          std::cout << "[Main] Quit requested via console" << std::endl;
          quitRequested = true;
          break;
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
  });

  // Run UI (or just wait for frontend connections)
  mainWindow.run();

  // Shutdown signals
  tickQueue.invalidate();

  // Shutdown
  binanceClient->shutdown();
  wsServer->stop();

  if (processingThread.joinable()) {
    processingThread.join();
  }
  if (networkThread.joinable()) {
    networkThread.join();
  }
  if (cleanupThread.joinable()) {
    cleanupThread.join();
  }

  database->close();
  
  std::cout << "Exiting correctly." << std::endl;
  return 0;
}
