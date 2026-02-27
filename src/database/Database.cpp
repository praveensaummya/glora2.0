#include "Database.h"
#include <iostream>
#include <chrono>
#include <sstream>
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
  
  const char* userSettingsTable = R"(
    CREATE TABLE IF NOT EXISTS user_settings (
      id INTEGER PRIMARY KEY CHECK (id = 1),
      api_key TEXT,
      api_secret TEXT,
      use_testnet INTEGER DEFAULT 0,
      created_at INTEGER DEFAULT (strftime('%s', 'now')),
      updated_at INTEGER DEFAULT (strftime('%s', 'now'))
    );
  )";
  
  const char* symbolsTable = R"(
    CREATE TABLE IF NOT EXISTS symbols (
      symbol TEXT PRIMARY KEY,
      base_asset TEXT NOT NULL,
      quote_asset TEXT NOT NULL,
      status TEXT NOT NULL,
      permissions TEXT NOT NULL,
      min_price REAL DEFAULT 0,
      max_price REAL DEFAULT 0,
      tick_size REAL DEFAULT 0,
      min_qty REAL DEFAULT 0,
      max_qty REAL DEFAULT 0,
      step_size REAL DEFAULT 0,
      min_notional REAL DEFAULT 0,
      last_price REAL DEFAULT 0,
      price_change REAL DEFAULT 0,
      price_change_percent REAL DEFAULT 0,
      high_24h REAL DEFAULT 0,
      low_24h REAL DEFAULT 0,
      volume_24h REAL DEFAULT 0,
      quote_volume_24h REAL DEFAULT 0,
      last_update_time INTEGER DEFAULT (strftime('%s', 'now'))
    );
    CREATE INDEX IF NOT EXISTS idx_symbols_quote_asset ON symbols(quote_asset);
    CREATE INDEX IF NOT EXISTS idx_symbols_base_asset ON symbols(base_asset);
    CREATE INDEX IF NOT EXISTS idx_symbols_volume ON symbols(quote_volume_24h DESC);
  )";
  
  execute(ticksTable);
  execute(candlesTable);
  execute(gapsTable);
  execute(userSettingsTable);
  execute(symbolsTable);
  
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
  
  // Validate SQLite handle before use
  if (!db_) return false;
  
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

bool Database::cleanupOldData(int keepDays) {
  if (!db_) return false;
  
  // Calculate cutoff timestamp (keepDays ago in milliseconds)
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
  ).count();
  
  uint64_t cutoffTime = now - (static_cast<uint64_t>(keepDays) * 24 * 60 * 60 * 1000);
  
  std::cout << "[Database] Cleaning up data older than " << keepDays 
            << " days (cutoff: " << cutoffTime << ")" << std::endl;
  
  // Delete old ticks
  std::stringstream ss;
  ss << "DELETE FROM ticks WHERE timestamp_ms < " << cutoffTime;
  bool success = execute(ss.str());
  
  // Delete old candles
  ss.str("");
  ss << "DELETE FROM candles WHERE start_time_ms < " << cutoffTime;
  success = execute(ss.str()) && success;
  
  // Delete old gaps
  ss.str("");
  ss << "DELETE FROM gaps WHERE start_time < " << cutoffTime;
  success = execute(ss.str()) && success;
  
  // Vacuum to reclaim space
  if (success) {
    execute("VACUUM");
    std::cout << "[Database] Cleanup completed successfully" << std::endl;
  }
  
  return success;
}

bool Database::saveApiCredentials(const std::string& apiKey, const std::string& apiSecret, bool useTestnet) {
  if (!db_) return false;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    INSERT OR REPLACE INTO user_settings (id, api_key, api_secret, use_testnet, updated_at)
    VALUES (1, ?, ?, ?, strftime('%s', 'now'))
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  sqlite3_bind_text(stmt, 1, apiKey.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, apiSecret.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 3, useTestnet ? 1 : 0);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  
  std::cout << "[Database] API credentials saved successfully" << std::endl;
  return rc == SQLITE_DONE;
}

bool Database::getApiCredentials(std::string& apiKey, std::string& apiSecret, bool& useTestnet) const {
  if (!db_) return false;
  
  sqlite3_stmt* stmt;
  const char* sql = "SELECT api_key, api_secret, use_testnet FROM user_settings WHERE id = 1";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    const char* key = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    const char* secret = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    
    if (key && secret) {
      apiKey = key;
      apiSecret = secret;
      useTestnet = sqlite3_column_int(stmt, 2) == 1;
      sqlite3_finalize(stmt);
      return true;
    }
  }
  
  sqlite3_finalize(stmt);
  return false;
}

bool Database::deleteApiCredentials() {
  if (!db_) return false;
  
  const char* sql = "DELETE FROM user_settings WHERE id = 1";
  sqlite3_stmt* stmt;
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  
  std::cout << "[Database] API credentials deleted" << std::endl;
  return rc == SQLITE_DONE;
}

bool Database::hasApiCredentials() const {
  std::string apiKey, apiSecret;
  bool useTestnet;
  return getApiCredentials(apiKey, apiSecret, useTestnet) && !apiKey.empty();
}

// === Symbol Metadata Operations ===

bool Database::insertOrUpdateSymbol(const core::Symbol& symbol) {
  if (!db_) return false;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    INSERT OR REPLACE INTO symbols 
    (symbol, base_asset, quote_asset, status, permissions, 
     min_price, max_price, tick_size, min_qty, max_qty, step_size, min_notional,
     last_update_time)
    VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, strftime('%s', 'now'))
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  sqlite3_bind_text(stmt, 1, symbol.symbol.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, symbol.baseAsset.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, symbol.quoteAsset.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, symbol.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, symbol.permissions.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 6, symbol.minPrice);
  sqlite3_bind_double(stmt, 7, symbol.maxPrice);
  sqlite3_bind_double(stmt, 8, symbol.tickSize);
  sqlite3_bind_double(stmt, 9, symbol.minQty);
  sqlite3_bind_double(stmt, 10, symbol.maxQty);
  sqlite3_bind_double(stmt, 11, symbol.stepSize);
  sqlite3_bind_double(stmt, 12, symbol.minNotional);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  
  return rc == SQLITE_DONE;
}

bool Database::insertSymbols(const std::vector<core::Symbol>& symbols) {
  if (symbols.empty() || !db_) return true;
  
  sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "BEGIN TRANSACTION", nullptr, nullptr, nullptr);
  
  for (const auto& symbol : symbols) {
    if (!insertOrUpdateSymbol(symbol)) {
      sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "COMMIT", nullptr, nullptr, nullptr);
      return false;
    }
  }
  
  sqlite3_exec(reinterpret_cast<sqlite3*>(db_), "COMMIT", nullptr, nullptr, nullptr);
  
  std::cout << "[Database] Inserted " << symbols.size() << " symbols" << std::endl;
  return true;
}

std::vector<core::Symbol> Database::getAllSymbols() const {
  std::vector<core::Symbol> symbols;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT symbol, base_asset, quote_asset, status, permissions,
           min_price, max_price, tick_size, min_qty, max_qty, step_size, min_notional,
           last_price, price_change, price_change_percent, high_24h, low_24h, volume_24h, quote_volume_24h
    FROM symbols
    ORDER BY quote_volume_24h DESC
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return symbols;
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    core::Symbol symbol;
    symbol.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    symbol.baseAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    symbol.quoteAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    symbol.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    symbol.permissions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    symbol.minPrice = sqlite3_column_double(stmt, 5);
    symbol.maxPrice = sqlite3_column_double(stmt, 6);
    symbol.tickSize = sqlite3_column_double(stmt, 7);
    symbol.minQty = sqlite3_column_double(stmt, 8);
    symbol.maxQty = sqlite3_column_double(stmt, 9);
    symbol.stepSize = sqlite3_column_double(stmt, 10);
    symbol.minNotional = sqlite3_column_double(stmt, 11);
    symbol.lastPrice = sqlite3_column_double(stmt, 12);
    symbol.priceChange = sqlite3_column_double(stmt, 13);
    symbol.priceChangePercent = sqlite3_column_double(stmt, 14);
    symbol.high24h = sqlite3_column_double(stmt, 15);
    symbol.low24h = sqlite3_column_double(stmt, 16);
    symbol.volume24h = sqlite3_column_double(stmt, 17);
    symbol.quoteVolume24h = sqlite3_column_double(stmt, 18);
    symbols.push_back(symbol);
  }
  
  sqlite3_finalize(stmt);
  return symbols;
}

std::optional<core::Symbol> Database::getSymbol(const std::string& symbolName) const {
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT symbol, base_asset, quote_asset, status, permissions,
           min_price, max_price, tick_size, min_qty, max_qty, step_size, min_notional,
           last_price, price_change, price_change_percent, high_24h, low_24h, volume_24h, quote_volume_24h
    FROM symbols
    WHERE symbol = ?
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return std::nullopt;
  
  sqlite3_bind_text(stmt, 1, symbolName.c_str(), -1, SQLITE_TRANSIENT);
  
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    core::Symbol symbol;
    symbol.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    symbol.baseAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    symbol.quoteAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    symbol.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    symbol.permissions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    symbol.minPrice = sqlite3_column_double(stmt, 5);
    symbol.maxPrice = sqlite3_column_double(stmt, 6);
    symbol.tickSize = sqlite3_column_double(stmt, 7);
    symbol.minQty = sqlite3_column_double(stmt, 8);
    symbol.maxQty = sqlite3_column_double(stmt, 9);
    symbol.stepSize = sqlite3_column_double(stmt, 10);
    symbol.minNotional = sqlite3_column_double(stmt, 11);
    symbol.lastPrice = sqlite3_column_double(stmt, 12);
    symbol.priceChange = sqlite3_column_double(stmt, 13);
    symbol.priceChangePercent = sqlite3_column_double(stmt, 14);
    symbol.high24h = sqlite3_column_double(stmt, 15);
    symbol.low24h = sqlite3_column_double(stmt, 16);
    symbol.volume24h = sqlite3_column_double(stmt, 17);
    symbol.quoteVolume24h = sqlite3_column_double(stmt, 18);
    sqlite3_finalize(stmt);
    return symbol;
  }
  
  sqlite3_finalize(stmt);
  return std::nullopt;
}

std::vector<core::Symbol> Database::getSymbolsByQuoteAsset(const std::string& quoteAsset) const {
  std::vector<core::Symbol> symbols;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT symbol, base_asset, quote_asset, status, permissions,
           min_price, max_price, tick_size, min_qty, max_qty, step_size, min_notional,
           last_price, price_change, price_change_percent, high_24h, low_24h, volume_24h, quote_volume_24h
    FROM symbols
    WHERE quote_asset = ?
    ORDER BY quote_volume_24h DESC
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return symbols;
  
  sqlite3_bind_text(stmt, 1, quoteAsset.c_str(), -1, SQLITE_TRANSIENT);
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    core::Symbol symbol;
    symbol.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    symbol.baseAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    symbol.quoteAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    symbol.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    symbol.permissions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    symbol.minPrice = sqlite3_column_double(stmt, 5);
    symbol.maxPrice = sqlite3_column_double(stmt, 6);
    symbol.tickSize = sqlite3_column_double(stmt, 7);
    symbol.minQty = sqlite3_column_double(stmt, 8);
    symbol.maxQty = sqlite3_column_double(stmt, 9);
    symbol.stepSize = sqlite3_column_double(stmt, 10);
    symbol.minNotional = sqlite3_column_double(stmt, 11);
    symbol.lastPrice = sqlite3_column_double(stmt, 12);
    symbol.priceChange = sqlite3_column_double(stmt, 13);
    symbol.priceChangePercent = sqlite3_column_double(stmt, 14);
    symbol.high24h = sqlite3_column_double(stmt, 15);
    symbol.low24h = sqlite3_column_double(stmt, 16);
    symbol.volume24h = sqlite3_column_double(stmt, 17);
    symbol.quoteVolume24h = sqlite3_column_double(stmt, 18);
    symbols.push_back(symbol);
  }
  
  sqlite3_finalize(stmt);
  return symbols;
}

std::vector<core::Symbol> Database::getSymbolsByBaseAsset(const std::string& baseAsset) const {
  std::vector<core::Symbol> symbols;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    SELECT symbol, base_asset, quote_asset, status, permissions,
           min_price, max_price, tick_size, min_qty, max_qty, step_size, min_notional,
           last_price, price_change, price_change_percent, high_24h, low_24h, volume_24h, quote_volume_24h
    FROM symbols
    WHERE base_asset = ?
    ORDER BY quote_volume_24h DESC
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return symbols;
  
  sqlite3_bind_text(stmt, 1, baseAsset.c_str(), -1, SQLITE_TRANSIENT);
  
  while (sqlite3_step(stmt) == SQLITE_ROW) {
    core::Symbol symbol;
    symbol.symbol = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
    symbol.baseAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 1));
    symbol.quoteAsset = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 2));
    symbol.status = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 3));
    symbol.permissions = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 4));
    symbol.minPrice = sqlite3_column_double(stmt, 5);
    symbol.maxPrice = sqlite3_column_double(stmt, 6);
    symbol.tickSize = sqlite3_column_double(stmt, 7);
    symbol.minQty = sqlite3_column_double(stmt, 8);
    symbol.maxQty = sqlite3_column_double(stmt, 9);
    symbol.stepSize = sqlite3_column_double(stmt, 10);
    symbol.minNotional = sqlite3_column_double(stmt, 11);
    symbol.lastPrice = sqlite3_column_double(stmt, 12);
    symbol.priceChange = sqlite3_column_double(stmt, 13);
    symbol.priceChangePercent = sqlite3_column_double(stmt, 14);
    symbol.high24h = sqlite3_column_double(stmt, 15);
    symbol.low24h = sqlite3_column_double(stmt, 16);
    symbol.volume24h = sqlite3_column_double(stmt, 17);
    symbol.quoteVolume24h = sqlite3_column_double(stmt, 18);
    symbols.push_back(symbol);
  }
  
  sqlite3_finalize(stmt);
  return symbols;
}

bool Database::updateSymbolPrice(const std::string& symbolName, double price, double priceChange, 
                                double priceChangePercent, double high24h, double low24h,
                                double volume24h, double quoteVolume24h) {
  if (!db_) return false;
  
  sqlite3_stmt* stmt;
  const char* sql = R"(
    UPDATE symbols SET 
      last_price = ?,
      price_change = ?,
      price_change_percent = ?,
      high_24h = ?,
      low_24h = ?,
      volume_24h = ?,
      quote_volume_24h = ?,
      last_update_time = strftime('%s', 'now')
    WHERE symbol = ?
  )";
  
  int rc = sqlite3_prepare_v2(reinterpret_cast<sqlite3*>(db_), sql, -1, &stmt, nullptr);
  if (rc != SQLITE_OK) return false;
  
  sqlite3_bind_double(stmt, 1, price);
  sqlite3_bind_double(stmt, 2, priceChange);
  sqlite3_bind_double(stmt, 3, priceChangePercent);
  sqlite3_bind_double(stmt, 4, high24h);
  sqlite3_bind_double(stmt, 5, low24h);
  sqlite3_bind_double(stmt, 6, volume24h);
  sqlite3_bind_double(stmt, 7, quoteVolume24h);
  sqlite3_bind_text(stmt, 8, symbolName.c_str(), -1, SQLITE_TRANSIENT);
  
  rc = sqlite3_step(stmt);
  sqlite3_finalize(stmt);
  
  return rc == SQLITE_DONE;
}

} // namespace database
} // namespace glora
