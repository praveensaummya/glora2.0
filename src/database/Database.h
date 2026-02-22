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
