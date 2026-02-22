#pragma once

#include <string>

namespace glora {
namespace settings {

// History duration options in days
enum class HistoryDuration {
  LAST_3_DAYS = 3,
  LAST_7_DAYS = 7,
  LAST_14_DAYS = 14,
  LAST_30_DAYS = 30,
  CUSTOM = -1
};

// API Configuration
struct ApiConfig {
  std::string apiKey;
  std::string apiSecret;
  bool useTestnet = false;
  
  bool isValid() const {
    return !apiKey.empty() && !apiSecret.empty();
  }
};

// Application settings
struct AppSettings {
  // API Settings
  ApiConfig binance;
  
  // Chart Settings
  std::string defaultSymbol = "BTCUSDT";
  HistoryDuration historyDuration = HistoryDuration::LAST_7_DAYS;
  int customDays = 7;
  
  // Window Settings
  int windowWidth = 1280;
  int windowHeight = 720;
  
  // Rendering Settings
  bool vsync = true;
  int targetFps = 60;
};

} // namespace settings
} // namespace glora
