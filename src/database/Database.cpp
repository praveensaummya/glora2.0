#include "Database.h"
#include <iostream>
#include <sqlite3.h>
#include <cstring>

namespace glora {
namespace database {

Database::Database() : db_(nullptr), dbPath_("") {}

Database::~Database() {
  close();
}

bool Database::initialize(const std::string& dbPath) {
  dbPath_ = dbPath;
  
  int rc = sqlite3_open(dbPath.c_str(), reinterpret_cast<sqlite3**>(&db_));
  if (rc) {
    std::cerr << "Can't open database: " << sqlite3_errmsg(reinterpret_cast<sqlite3*>(db_)) << std::endl;
    return false;
  }
  
  // Enable WAL mode for better performance
  execute("PRAGMA journal_mode=WAL;");
  execute("PRAGMA synchronous=NORMAL;");
  execute("PRAGMA cache_size=10000;");
  
  // Create tables
  const char* ticksTable = R"(
    CREATE TABLE IF NOT EXISTS ticks (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      symbol TEXT NOT NULL,
      timestamp_ms INTEGER NOT NULL,
      price REAL NOT NULL,
      quantity REAL NOT NULL,
      is_buyer_maker INTEGER NOT NULL,
      UNIQUE(symbol, timestamp_ms, price, quantity)
    );
    CREATE INDEX IF NOT EXISTS idx_ticks_symbol_time ON ticks(symbol, timestamp_ms);
  )";
  
  const char* candlesTable = R"(
    CREATE TABLE IF NOT EXISTS candles (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      symbol TEXT NOT NULL,
      start_time_ms INTEGER NOT NULL,
      end_time_ms INTEGER NOT NULL,
      open REAL NOT NULL,
      high REAL NOT NULL,
      low REAL NOT NULL,
      close REAL NOT NULL,
      volume REAL NOT NULL,
      UNIQUE(symbol, start_time_ms)
    );
    CREATE INDEX IF NOT EXISTS idx_candles_symbol_time ON candles(symbol, start_time_ms);
  )";
  
  const char* gapsTable = R"(
    CREATE TABLE IF NOT EXISTS gaps (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      symbol TEXT NOT NULL,
      start_time INTEGER NOT NULL,
      end_time INTEGER NOT NULL,
      filled INTEGER DEFAULT 0,
      created_at INTEGER DEFAULT (strftime('%s', 'now')),
      UNIQUE(symbol, start_time)
    );
    CREATE INDEX IF NOT EXISTS idx_gaps_symbol ON gaps(symbol, filled);
  )";
  
  execute(ticksTable);
  execute(candlesTable);
  execute(gapsTable);
  
  std::cout << "Database initialized: " << dbPath << std::endl;
  return true;
}

void Database::close() {
  if (db_) {
    sqlite3_close(reinterpret_cast<sqlite3*>(db_));
    db_ = nullptr;
  }
}

bool Database::execute(const std::string& sql) const {
  char* errMsg = nullptr;
  int rc = sqlite3_exec(reinterpret_cast<sqlite3*>(db_), sql.c_str(), nullptr, nullptr, &errMsg);
  
  if (rc != SQLITE_OK) {
    std::cerr << "SQL error: " << errMsg << std::endl;
    sqlite3_free(errMsg);
    return false;
  }
  return true;
}

bool Database::insertTicks(const std::string& symbol, const std::vector<core::Tick>& ticks) {
  if (ticks.empty() || !db_) return true;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    INSERT OR IGNORE INTO ticks (symbol, timestamp_ms, price, quantity, is_buyer_maker)
    VALUES (?, ?, ?, ?, ?)
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  
  for (const auto& tick : ticks) {
    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, tick.timestamp_ms);
    sqlite3_bind_double(stmt, 3, tick.price);
    sqlite3_bind_double(stmt, 4, tick.quantity);
    sqlite3_bind_int(stmt, 5, tick.is_buyer_maker ? 1 : 0);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  
  sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "COMMIT", nullptr, nullptr, nullptr);
  sqlite3_finalize(stmt);
  
  return true;
}

std::vector<core::Tick> Database::getTicks(const std::string& symbol, uint64_t startTime, uint64_t endTime) const {
  std::vector<core::Tick> ticks;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT timestamp_ms, price, quantity, is_buyer_maker
    FROM ticks
    WHERE symbol = ? AND timestamp_ms >= ? AND timestamp_ms <= ?
    ORDER BY timestamp_ms ASC
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return ticks;
  
  sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, startTime);
  sqlite3_bind_int64(stmt, 3, endTime);
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    core::Tick tick;
    tick.timestamp_ms = sqlite3_column_int64(stmt, 0);
    tick.price = sqlite3_column_double(stmt, 1);
    tick.quantity = sqlite3_column_double(stmt, 2);
    tick.is_buyer_maker = sqlite3_column_int(stmt, 3) == 1;
    ticks.push_back(tick);
  }
  
  sqlite3_finalize(stmt);
  return ticks;
}

std::optional<uint64_t> Database::getLatestTickTime(const std::string& symbol) const {
  sqlite3_stmt* stmt;
  const char* sql = "SELECT MAX(timestamp_ms) FROM ticks WHERE symbol = ?";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return std::nullopt;
  
  sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
  
  if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
    uint64_t time = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return time;
  }
  
  sqlite3_finalize(stmt);
  return std::nullopt;
}

std::optional<uint64_t> Database::getEarliestTickTime(const std::string& symbol) const {
  sqlite3_stmt* stmt;
  const char* sql = "SELECT MIN(timestamp_ms) FROM ticks WHERE symbol = ?";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return std::nullopt;
  
  sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
  
  if (sqlite3_step(stmt) == SQLITE_ROW && sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
    uint64_t time = sqlite3_column_int64(stmt, 0);
    sqlite3_finalize(stmt);
    return time;
  }
  
  sqlite3_finalize(stmt);
  return std::nullopt;
}

bool Database::insertCandles(const std::string& symbol, const std::vector<core::Candle>& candles) {
  if (candles.empty() || !db_) return true;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    INSERT OR REPLACE INTO candles 
    (symbol, start_time_ms, end_time_ms, open, high, low, close, volume)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?)
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  
  for (const auto& candle : candles) {
    sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 2, candle.start_time_ms);
    sqlite3_bind_int64(stmt, 3, candle.end_time_ms);
    sqlite3_bind_double(stmt, 4, candle.open);
    sqlite3_bind_double(stmt, 5, candle.high);
    sqlite3_bind_double(stmt, 6, candle.low);
    sqlite3_bind_double(stmt, 7, candle.close);
    sqlite3_bind_double(stmt, 8, candle.volume);
    sqlite3_step(stmt);
    sqlite3_reset(stmt);
  }
  
  sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "COMMIT", nullptr, nullptr, nullptr);
  sqlite3_finalize(stmt);
  
  return true;
}

std::vector<core::Candle> Database::getCandles(const std::string& symbol, uint64_t startTime, uint64_t endTime) const {
  std::vector<core::Candle> candles;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT start_time_ms, end_time_ms, open, high, low, close, volume
    FROM candles
    WHERE symbol = ? AND start_time_ms >= ? AND start_time_ms <= ?
    ORDER BY start_time_ms ASC
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return candles;
  
  sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, startTime);
  sqlite3_bind_int64(stmt, 3, endTime);
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    core::Candle candle;
    candle.start_time_ms = sqlite3_column_int64(stmt, 0);
    candle.end_time_ms = sqlite3_column_int64(stmt, 1);
    candle.open = sqlite3_column_double(stmt, 2);
    candle.high = sqlite3_column_double(stmt, 3);
    candle.low = sqlite3_column_double(stmt, 4);
    candle.close = sqlite3_column_double(stmt, 5);
    candle.volume = sqlite3_column_double(stmt, 6);
    candles.push_back(candle);
  }
  
  sqlite3_finalize(stmt);
  return candles;
}

std::vector<DataGap> Database::detectGaps(const std::string& symbol, uint64_t startTime, uint64_t endTime, uint64_t maxGapMs) const {
  std::vector<DataGap> gaps;
  
  // Get all timestamps in range
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT timestamp_ms FROM ticks
    WHERE symbol = ? AND timestamp_ms >= ? AND timestamp_ms <= ?
    ORDER BY timestamp_ms ASC
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return gaps;
  
  sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, startTime);
  sqlite3_bind_int64(stmt, 3, endTime);
  
  uint64_t prevTime = 0;
  bool first = true;
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    uint64_t currentTime = sqlite3_column_int64(stmt, 0);
    
    if (!first) {
      uint64_t gap = currentTime - prevTime;
      if (gap > maxGapMs) {
        gaps.push_back({symbol, prevTime, currentTime});
      }
    } else {
      first = false;
      // Check gap from requested start time
      if (startTime > 0 && (prevTime == 0 || prevTime > startTime)) {
        if (currentTime - startTime > maxGapMs) {
          gaps.push_back({symbol, startTime, currentTime});
        }
      }
    }
    prevTime = currentTime;
  }
  
  sqlite3_finalize(stmt);
  return gaps;
}

bool Database::markGapFilled(const std::string& symbol, uint64_t startTime, uint64_t endTime) {
  const char* sql = R"(
    UPDATE gaps SET filled = 1 
    WHERE symbol = ? AND start_time = ? AND end_time = ?
  )";
  
  sqlite3_stmt* stmt;
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  sqlite3_bind_text(stmt, 1, symbol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, startTime);
  sqlite3_bind_int64(stmt, 3, endTime);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  
  return rc == SQLITE_DONE;
}

bool Database::deleteSymbolData(const std::string& symbol) {
  execute("DELETE FROM ticks WHERE symbol = '" + symbol + "'");
  execute("DELETE FROM candles WHERE symbol = '" + symbol + "'");
  execute("DELETE FROM gaps WHERE symbol = '" + symbol + "'");
  return true;
}

} // namespace database
} // namespace glora
