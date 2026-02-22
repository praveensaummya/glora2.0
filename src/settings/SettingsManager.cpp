#include "SettingsManager.h"
#include <iostream>
#include <filesystem>
#include <cstdlib>

namespace glora {
namespace settings {

using json = nlohmann::json;

SettingsManager& SettingsManager::getInstance() {
  static SettingsManager instance;
  return instance;
}

std::string SettingsManager::getSettingsPath() const {
  return settingsPath_;
}

std::string SettingsManager::getDatabasePath() const {
  return databasePath_;
}

void SettingsManager::toJson(json& j) const {
  j = json{
    {"binance", {
      {"apiKey", settings_.binance.apiKey},
      {"apiSecret", settings_.binance.apiSecret},
      {"useTestnet", settings_.binance.useTestnet}
    }},
    {"chart", {
      {"defaultSymbol", settings_.defaultSymbol},
      {"historyDuration", static_cast<int>(settings_.historyDuration)},
      {"customDays", settings_.customDays}
    }},
    {"window", {
      {"width", settings_.windowWidth},
      {"height", settings_.windowHeight}
    }},
    {"rendering", {
      {"vsync", settings_.vsync},
      {"targetFps", settings_.targetFps}
    }}
  };
}

void SettingsManager::fromJson(const json& j) {
  // Binance settings
  if (j.contains("binance")) {
    const auto& binance = j["binance"];
    settings_.binance.apiKey = binance.value("apiKey", "");
    settings_.binance.apiSecret = binance.value("apiSecret", "");
    settings_.binance.useTestnet = binance.value("useTestnet", false);
  }
  
  // Chart settings
  if (j.contains("chart")) {
    const auto& chart = j["chart"];
    settings_.defaultSymbol = chart.value("defaultSymbol", "BTCUSDT");
    settings_.historyDuration = static_cast<HistoryDuration>(
      chart.value("historyDuration", static_cast<int>(HistoryDuration::LAST_7_DAYS))
    );
    settings_.customDays = chart.value("customDays", 7);
  }
  
  // Window settings
  if (j.contains("window")) {
    const auto& window = j["window"];
    settings_.windowWidth = window.value("width", 1280);
    settings_.windowHeight = window.value("height", 720);
  }
  
  // Rendering settings
  if (j.contains("rendering")) {
    const auto& rendering = j["rendering"];
    settings_.vsync = rendering.value("vsync", true);
    settings_.targetFps = rendering.value("targetFps", 60);
  }
}

bool SettingsManager::load(const std::string& filepath) {
  // Determine paths
  std::string homeDir;
  if (const char* envHome = std::getenv("HOME")) {
    homeDir = envHome;
  } else if (const char* envUserProfile = std::getenv("USERPROFILE")) {
    homeDir = envUserProfile;
  } else {
    homeDir = ".";
  }
  
  std::string configDir = homeDir + "/.glora";
  std::filesystem::create_directories(configDir);
  
  settingsPath_ = filepath.empty() ? configDir + "/settings.json" : filepath;
  databasePath_ = configDir + "/history.db";
  
  // Try to load existing settings
  try {
    std::ifstream file(settingsPath_);
    if (file.is_open()) {
      json j;
      file >> j;
      fromJson(j);
      std::cout << "Settings loaded from: " << settingsPath_ << std::endl;
      return true;
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to load settings: " << e.what() << std::endl;
  }
  
  // Use defaults if file doesn't exist
  std::cout << "Using default settings" << std::endl;
  return false;
}

bool SettingsManager::save(const std::string& filepath) {
  std::string path = filepath.empty() ? settingsPath_ : filepath;
  
  try {
    json j;
    toJson(j);
    
    std::ofstream file(path);
    if (file.is_open()) {
      file << j.dump(2);
      settingsPath_ = path;
      std::cout << "Settings saved to: " << path << std::endl;
      return true;
    }
  } catch (const std::exception& e) {
    std::cerr << "Failed to save settings: " << e.what() << std::endl;
  }
  
  return false;
}

} // namespace settings
} // namespace glora
