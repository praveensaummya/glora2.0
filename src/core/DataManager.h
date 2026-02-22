#pragma once

#include "DataModels.h"
#include "../database/Database.h"
#include "../network/BinanceClient.h"
#include "../settings/Settings.h"
#include <memory>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>

namespace glora {
namespace core {

// Forward declarations
class DataManager;

// Callback for data updates
using OnDataUpdateCallback = std::function<void()>;
using OnGapFilledCallback = std::function<void(uint64_t, uint64_t)>;

class DataManager {
public:
  DataManager();
  ~DataManager();
  
  // Initialize with settings
  bool initialize(const settings::AppSettings& settings);
  
  // Set network client
  void setNetworkClient(std::shared_ptr<network::BinanceClient> client);
  
  // Set database
  void setDatabase(std::shared_ptr<database::Database> db);
  
  // Load data for a symbol (from DB + fetch missing)
  void loadSymbolData(const std::string& symbol);
  
  // Add a live tick (from WebSocket)
  void addLiveTick(const Tick& tick);
  
  // Get all candles for a symbol
  const std::vector<Candle>& getCandles(const std::string& symbol) const;
  
  // Get all ticks for a symbol within time range
  std::vector<Tick> getTicks(const std::string& symbol, uint64_t startTime, uint64_t endTime) const;
  
  // Get latest tick time in database
  std::optional<uint64_t> getLatestTickTime(const std::string& symbol) const;
  
  // Register for data updates
  void setOnDataUpdateCallback(OnDataUpdateCallback callback);
  void setOnGapFilledCallback(OnGapFilledCallback callback);
  
  // Get current symbol
  const std::string& getCurrentSymbol() const { return currentSymbol_; }
  
  // Check if gap filling is in progress
  bool isLoadingHistory() const { return isLoadingHistory_.load(); }
  
  // Force refresh data from API
  void refreshData();

private:
  void loadFromDatabase();
  void detectAndFillGaps();
  void fetchMissingData(uint64_t startTime, uint64_t endTime);
  void processTicksToCandles(const std::vector<Tick>& ticks);
  
  std::string currentSymbol_;
  std::shared_ptr<network::BinanceClient> networkClient_;
  std::shared_ptr<database::Database> database_;
  settings::AppSettings settings_;
  
  // Cached candles
  std::map<std::string, std::vector<Candle>> candlesBySymbol_;
  mutable std::mutex dataMutex_;
  
  // Callbacks
  OnDataUpdateCallback onDataUpdate_;
  OnGapFilledCallback onGapFilled_;
  
  // State
  std::atomic<bool> isLoadingHistory_{false};
  std::atomic<bool> isInitialized_{false};
};

} // namespace core
} // namespace glora
