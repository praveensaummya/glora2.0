#pragma once

#include "Settings.h"
#include <string>
#include <fstream>
#include <nlohmann/json.hpp>

namespace glora {
namespace settings {

class SettingsManager {
public:
  static SettingsManager& getInstance();
  
  // Load settings from file
  bool load(const std::string& filepath = "");
  
  // Save settings to file
  bool save(const std::string& filepath = "");
  
  // Get current settings
  AppSettings& getSettings() { return settings_; }
  const AppSettings& getSettings() const { return settings_; }
  
  // Update settings
  void updateSettings(const AppSettings& settings) { settings_ = settings; }
  
  // Get settings file path
  std::string getSettingsPath() const;
  
  // Get database path
  std::string getDatabasePath() const;

private:
  SettingsManager() = default;
  ~SettingsManager() = default;
  SettingsManager(const SettingsManager&) = delete;
  SettingsManager& operator=(const SettingsManager&) = delete;
  
  AppSettings settings_;
  std::string settingsPath_;
  std::string databasePath_;
  
  // JSON serialization helpers
  void toJson(nlohmann::json& j) const;
  void fromJson(const nlohmann::json& j);
};

} // namespace settings
} // namespace glora
