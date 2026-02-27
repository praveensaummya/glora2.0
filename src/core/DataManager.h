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
#include <unordered_map>
#include <set>

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
  
  // Add a live tick (from WebSocket) - with explicit symbol
  void addLiveTick(const std::string& symbol, const Tick& tick);
  
  // Add a live tick using current symbol (backwards compatible)
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
  
  // === Symbol Management (flat_map for high-performance) ===
  
  // Load all symbols from database/API
  void loadSymbols();
  
  // Get all symbols
  std::vector<Symbol> getAllSymbols() const;
  
  // Get symbol by name
  const Symbol* getSymbol(const std::string& symbol) const;
  
  // Get symbols by quote asset (secondary index)
  std::vector<Symbol> getSymbolsByQuoteAsset(const std::string& quoteAsset) const;
  
  // Get symbols by base asset (secondary index)
  std::vector<Symbol> getSymbolsByBaseAsset(const std::string& baseAsset) const;
  
  // Update symbol price (from miniTicker)
  void updateSymbolPrice(const std::string& symbol, double price, double priceChange,
                        double priceChangePercent, double high24h, double low24h,
                        double volume24h, double quoteVolume24h);
  
  // Fetch exchange info from API and store in DB
  void fetchExchangeInfoFromApi();
  
  // Get all quote assets (for filtering)
  std::vector<std::string> getQuoteAssets() const;
  
  // Get all base assets (for filtering)
  std::vector<std::string> getBaseAssets() const;
  
  // === Multi-timeframe candle aggregation ===
  // Aggregate 1m candles to higher timeframes (5m, 15m, 1h, 4h, 1D)
  std::vector<Candle> aggregateToTimeframe(const std::string& symbol, const std::string& interval) const;

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
  
  // === Symbol Storage with flat_map and secondary indices ===
  // Using flat_map (sorted vector) for cache locality
  flat_map<std::string, Symbol> symbols_;
  // Secondary indices for filtering
  std::unordered_map<std::string, std::vector<std::string>> symbolsByQuoteAsset_;  // quoteAsset -> symbols
  std::unordered_map<std::string, std::vector<std::string>> symbolsByBaseAsset_;    // baseAsset -> symbols
  mutable std::mutex symbolMutex_;
  
  // State
  std::atomic<bool> isLoadingHistory_{false};
  std::atomic<bool> isInitialized_{false};
};

} // namespace core
} // namespace glora
