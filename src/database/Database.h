#pragma once

#include "../core/DataModels.h"
#include <string>
#include <vector>
#include <optional>
#include <cstdint>

namespace glora {
namespace database {

// Represents a gap in the data
struct DataGap {
  std::string symbol;
  uint64_t startTime;
  uint64_t endTime;
};

class Database {
public:
  Database();
  ~Database();
  
  // Initialize database with file path
  bool initialize(const std::string& dbPath);
  
  // Close database connection
  void close();
  
  // === Tick Data Operations ===
  
  // Insert multiple ticks (bulk insert for efficiency)
  bool insertTicks(const std::string& symbol, const std::vector<core::Tick>& ticks);
  
  // Get ticks within time range
  std::vector<core::Tick> getTicks(const std::string& symbol, 
                                    uint64_t startTime, 
                                    uint64_t endTime) const;
  
  // Get latest tick time for a symbol
  std::optional<uint64_t> getLatestTickTime(const std::string& symbol) const;
  
  // Get earliest tick time for a symbol
  std::optional<uint64_t> getEarliestTickTime(const std::string& symbol) const;
  
  // === Candle Data Operations ===
  
  // Insert candles
  bool insertCandles(const std::string& symbol, const std::vector<core::Candle>& candles);
  
  // Get candles within time range
  std::vector<core::Candle> getCandles(const std::string& symbol,
                                        uint64_t startTime,
                                        uint64_t endTime) const;
  
  // === Gap Detection ===
  
  // Detect gaps in the data (time periods with no data)
  std::vector<DataGap> detectGaps(const std::string& symbol,
                                   uint64_t startTime,
                                   uint64_t endTime,
                                   uint64_t maxGapMs = 60000) const; // Default 1 minute gap
  
  // Mark gap as being filled
  bool markGapFilled(const std::string& symbol, uint64_t startTime, uint64_t endTime);
  
  // === Utility ===
  
  // Delete all data for a symbol
  bool deleteSymbolData(const std::string& symbol);
  
  // Cleanup old data (older than specified days)
  bool cleanupOldData(int keepDays = 7);
  
  // === User API Credentials ===
  
  // Save user API credentials
  bool saveApiCredentials(const std::string& apiKey, const std::string& apiSecret, bool useTestnet);
  
  // Get saved API credentials (returns false if not found)
  bool getApiCredentials(std::string& apiKey, std::string& apiSecret, bool& useTestnet) const;
  
  // Delete saved API credentials
  bool deleteApiCredentials();
  
  // Check if API credentials exist
  bool hasApiCredentials() const;
  
  // === Symbol Metadata Operations ===
  
  // Insert or update symbol metadata
  bool insertOrUpdateSymbol(const core::Symbol& symbol);
  
  // Insert multiple symbols (bulk)
  bool insertSymbols(const std::vector<core::Symbol>& symbols);
  
  // Get all symbols
  std::vector<core::Symbol> getAllSymbols() const;
  
  // Get symbol by name
  std::optional<core::Symbol> getSymbol(const std::string& symbol) const;
  
  // Get symbols by quote asset (e.g., "USDT", "BUSD")
  std::vector<core::Symbol> getSymbolsByQuoteAsset(const std::string& quoteAsset) const;
  
  // Get symbols by base asset (e.g., "BTC", "ETH")
  std::vector<core::Symbol> getSymbolsByBaseAsset(const std::string& baseAsset) const;
  
  // Update symbol price (real-time)
  bool updateSymbolPrice(const std::string& symbol, double price, double priceChange, 
                        double priceChangePercent, double high24h, double low24h,
                        double volume24h, double quoteVolume24h);
  
  // Get database file path
  std::string getPath() const { return dbPath_; }

private:
  std::string dbPath_;
  void* db_; // SQLite database handle (sqlite3*)
  
  // Internal helpers
  bool execute(const std::string& sql) const;
  std::string getTickInsertSql() const;
  std::string getCandleInsertSql() const;
};

} // namespace database
} // namespace glora
