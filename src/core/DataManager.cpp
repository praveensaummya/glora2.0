#include "DataManager.h"
#include <iostream>
#include <chrono>
#include <thread>

namespace glora {
namespace core {

DataManager::DataManager() {}

DataManager::~DataManager() {}

bool DataManager::initialize(const settings::AppSettings& settings) {
  settings_ = settings;
  isInitialized_ = true;
  currentSymbol_ = settings.defaultSymbol;
  std::cout << "DataManager initialized for symbol: " << currentSymbol_ << std::endl;
  return true;
}

void DataManager::setNetworkClient(std::shared_ptr<network::BinanceClient> client) {
  networkClient_ = client;
}

void DataManager::setDatabase(std::shared_ptr<database::Database> db) {
  database_ = db;
}

void DataManager::loadSymbolData(const std::string& symbol) {
  if (!isInitialized_) {
    std::cerr << "DataManager not initialized" << std::endl;
    return;
  }
  
  currentSymbol_ = symbol;
  loadFromDatabase();
  
  // Detect and fill gaps in a background thread
  std::thread gapThread([this]() {
    detectAndFillGaps();
  });
  gapThread.join(); // Properly join the thread instead of detaching
}

void DataManager::loadFromDatabase() {
  if (!database_) return;
  
  std::lock_guard<std::mutex> lock(dataMutex_);
  
  // Calculate time range based on settings
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
  ).count();
  
  int days = 7;
  if (settings_.historyDuration == settings::HistoryDuration::CUSTOM) {
    days = settings_.customDays;
  } else {
    days = static_cast<int>(settings_.historyDuration);
  }
  
  uint64_t startTime = now - (static_cast<uint64_t>(days) * 24 * 60 * 60 * 1000);
  
  // Load candles from DB
  auto candles = database_->getCandles(currentSymbol_, startTime, now);
  candlesBySymbol_[currentSymbol_] = candles;
  
  std::cout << "Loaded " << candles.size() << " candles from database" << std::endl;
}

void DataManager::detectAndFillGaps() {
  if (!database_ || !networkClient_) return;
  
  isLoadingHistory_ = true;
  
  uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
    std::chrono::system_clock::now().time_since_epoch()
  ).count();
  
  int days = 7;
  if (settings_.historyDuration == settings::HistoryDuration::CUSTOM) {
    days = settings_.customDays;
  } else {
    days = static_cast<int>(settings_.historyDuration);
  }
  
  uint64_t startTime = now - (static_cast<uint64_t>(days) * 24 * 60 * 60 * 1000);
  
  // Check if we have data in DB
  auto latestTime = database_->getLatestTickTime(currentSymbol_);
  auto earliestTime = database_->getEarliestTickTime(currentSymbol_);
  
  if (!latestTime.has_value()) {
    // No data at all, fetch everything
    std::cout << "No local data found, fetching full history..." << std::endl;
    fetchMissingData(startTime, now);
  } else if (!earliestTime.has_value() || earliestTime.value() > startTime) {
    // Missing data at the beginning
    std::cout << "Fetching historical data from beginning..." << std::endl;
    fetchMissingData(startTime, latestTime.value());
  } else {
    // Check for gaps in the middle
    auto gaps = database_->detectGaps(currentSymbol_, startTime, latestTime.value());
    
    if (!gaps.empty()) {
      std::cout << "Found " << gaps.size() << " gaps in data" << std::endl;
      for (const auto& gap : gaps) {
        // Only fill gaps larger than 60 seconds - smaller gaps will be filled by live data
        uint64_t gapSize = gap.endTime - gap.startTime;
        if (gapSize < 60000) {
          std::cout << "Skipping small gap (" << gapSize << "ms < 60s), live data will fill it" << std::endl;
          continue;
        }
        std::cout << "Gap: " << gap.startTime << " - " << gap.endTime << " (" << gapSize << "ms)" << std::endl;
        fetchMissingData(gap.startTime, gap.endTime);
      }
    } else {
      std::cout << "No gaps found in data" << std::endl;
    }
    
    // Only fetch data after latest known time if gap is more than 5 minutes
    // Otherwise rely on live data stream
    if (latestTime.value() < now - 300000) { // More than 5 minutes ago
      std::cout << "Fetching latest missing data (gap > 5 min)..." << std::endl;
      fetchMissingData(latestTime.value(), now);
    } else {
      std::cout << "Data is recent enough, relying on live data stream" << std::endl;
    }
  }
  
  // Reload data from DB
  loadFromDatabase();
  
  if (onDataUpdate_) {
    onDataUpdate_();
  }
  
  isLoadingHistory_ = false;
}

void DataManager::fetchMissingData(uint64_t startTime, uint64_t endTime) {
  if (!networkClient_) return;
  
  std::cout << "Fetching data from " << startTime << " to " << endTime << std::endl;
  
  std::vector<Tick> fetchedTicks;
  
  // Fetch historical trades
  networkClient_->fetchHistoricalAggTrades(
    currentSymbol_,
    startTime,
    endTime,
    [&fetchedTicks](const std::vector<Tick>& ticks) {
      fetchedTicks = ticks;
    }
  );
  
  if (!fetchedTicks.empty()) {
    // Save to database
    if (database_) {
      database_->insertTicks(currentSymbol_, fetchedTicks);
    }
    
    // Process into candles
    processTicksToCandles(fetchedTicks);
    
    // Notify gap filled
    if (onGapFilled_) {
      onGapFilled_(startTime, endTime);
    }
    
    std::cout << "Saved " << fetchedTicks.size() << " ticks to database" << std::endl;
  }
}

void DataManager::processTicksToCandles(const std::vector<Tick>& ticks) {
  if (ticks.empty()) return;
  
  // Group ticks into candles (1-minute candles for now)
  const uint64_t candleInterval = 60000; // 1 minute
  
  std::map<uint64_t, std::vector<Tick>> ticksByCandle;
  
  for (const auto& tick : ticks) {
    uint64_t candleStart = (tick.timestamp_ms / candleInterval) * candleInterval;
    ticksByCandle[candleStart].push_back(tick);
  }
  
  std::vector<Candle> candles;
  
  for (const auto& [startTime, candleTicks] : ticksByCandle) {
    Candle candle;
    candle.start_time_ms = startTime;
    candle.end_time_ms = startTime + candleInterval;
    
    for (const auto& tick : candleTicks) {
      candle.add_tick(tick);
    }
    
    candles.push_back(candle);
  }
  
  // Save candles to database
  if (database_) {
    database_->insertCandles(currentSymbol_, candles);
  }
  
  // Update cached data
  {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto& existingCandles = candlesBySymbol_[currentSymbol_];
    
    // Merge new candles with existing
    for (const auto& newCandle : candles) {
      bool found = false;
      for (auto& existing : existingCandles) {
        if (existing.start_time_ms == newCandle.start_time_ms) {
          existing = newCandle;
          found = true;
          break;
        }
      }
      if (!found) {
        existingCandles.push_back(newCandle);
      }
    }
    
    // Sort by time
    std::sort(existingCandles.begin(), existingCandles.end(),
              [](const Candle& a, const Candle& b) {
                return a.start_time_ms < b.start_time_ms;
              });
  }
}

void DataManager::addLiveTick(const std::string& symbol, const Tick& tick) {
  // Create a single-tick candle for real-time update
  Candle candle;
  candle.add_tick(tick);
  candle.start_time_ms = (tick.timestamp_ms / 60000) * 60000;
  candle.end_time_ms = candle.start_time_ms + 60000;
  
  {
    std::lock_guard<std::mutex> lock(dataMutex_);
    auto& candles = candlesBySymbol_[symbol];
    
    // Update or add the latest candle
    if (!candles.empty() && candles.back().start_time_ms == candle.start_time_ms) {
      // Add tick to existing candle
      candles.back().add_tick(tick);
      
      // Update candle in database
      if (database_) {
        database_->insertCandles(symbol, {candles.back()});
      }
    } else {
      candles.push_back(candle);
      
      // Insert new candle to database
      if (database_) {
        database_->insertCandles(symbol, {candle});
      }
    }
    
    // Keep only last N candles in memory
    const size_t maxCandlesInMemory = 10000;
    if (candles.size() > maxCandlesInMemory) {
      candles.erase(candles.begin(), candles.end() - maxCandlesInMemory);
    }
  }
  
  // Save tick to database (for raw tick data)
  if (database_) {
    database_->insertTicks(symbol, {tick});
  }
  
  if (onDataUpdate_) {
    onDataUpdate_();
  }
}

void DataManager::addLiveTick(const Tick& tick) {
  // Use current symbol for backwards compatibility
  addLiveTick(currentSymbol_, tick);
}

const std::vector<Candle>& DataManager::getCandles(const std::string& symbol) const {
  static std::vector<Candle> empty;
  auto it = candlesBySymbol_.find(symbol);
  if (it != candlesBySymbol_.end()) {
    return it->second;
  }
  return empty;
}

std::vector<Tick> DataManager::getTicks(const std::string& symbol, uint64_t startTime, uint64_t endTime) const {
  if (database_) {
    return database_->getTicks(symbol, startTime, endTime);
  }
  return {};
}

std::optional<uint64_t> DataManager::getLatestTickTime(const std::string& symbol) const {
  if (database_) {
    return database_->getLatestTickTime(symbol);
  }
  return std::nullopt;
}

void DataManager::setOnDataUpdateCallback(OnDataUpdateCallback callback) {
  onDataUpdate_ = callback;
}

void DataManager::setOnGapFilledCallback(OnGapFilledCallback callback) {
  onGapFilled_ = callback;
}

void DataManager::refreshData() {
  std::thread refreshThread([this]() {
    detectAndFillGaps();
  });
  refreshThread.join(); // Properly join the thread instead of detaching
}

// === Symbol Management ===

void DataManager::loadSymbols() {
  // First try to load from database
  if (database_) {
    auto dbSymbols = database_->getAllSymbols();
    if (!dbSymbols.empty()) {
      std::lock_guard<std::mutex> lock(symbolMutex_);
      for (auto& sym : dbSymbols) {
        symbols_[sym.symbol] = sym;
        // Build secondary indices
        symbolsByQuoteAsset_[sym.quoteAsset].push_back(sym.symbol);
        symbolsByBaseAsset_[sym.baseAsset].push_back(sym.symbol);
      }
      std::cout << "[DataManager] Loaded " << symbols_.size() << " symbols from database" << std::endl;
      return;
    }
  }
  
  // If database is empty, fetch from API
  fetchExchangeInfoFromApi();
}

void DataManager::fetchExchangeInfoFromApi() {
  if (!networkClient_) {
    std::cerr << "[DataManager] No network client available for fetching exchange info" << std::endl;
    return;
  }
  
  std::cout << "[DataManager] Fetching exchange info from API..." << std::endl;
  
  networkClient_->fetchExchangeInfo([this](const std::vector<Symbol>& apiSymbols) {
    if (!apiSymbols.empty()) {
      // Store in database
      if (database_) {
        database_->insertSymbols(apiSymbols);
      }
      
      // Update in-memory storage with flat_map
      {
        std::lock_guard<std::mutex> lock(symbolMutex_);
        symbols_.clear();
        symbolsByQuoteAsset_.clear();
        symbolsByBaseAsset_.clear();
        
        for (const auto& sym : apiSymbols) {
          symbols_[sym.symbol] = sym;
          // Build secondary indices
          symbolsByQuoteAsset_[sym.quoteAsset].push_back(sym.symbol);
          symbolsByBaseAsset_[sym.baseAsset].push_back(sym.symbol);
        }
      }
      
      std::cout << "[DataManager] Loaded " << apiSymbols.size() << " symbols from API" << std::endl;
      
      if (onDataUpdate_) {
        onDataUpdate_();
      }
    }
  });
}

std::vector<Symbol> DataManager::getAllSymbols() const {
  std::lock_guard<std::mutex> lock(symbolMutex_);
  std::vector<Symbol> result;
  for (const auto& pair : symbols_) {
    result.push_back(pair.second);
  }
  // Sort by quote volume descending
  std::sort(result.begin(), result.end(), [](const Symbol& a, const Symbol& b) {
    return a.quoteVolume24h > b.quoteVolume24h;
  });
  return result;
}

const Symbol* DataManager::getSymbol(const std::string& symbolName) const {
  std::lock_guard<std::mutex> lock(symbolMutex_);
  auto it = symbols_.find(symbolName);
  if (it != symbols_.end()) {
    return &it->second;
  }
  return nullptr;
}

std::vector<Symbol> DataManager::getSymbolsByQuoteAsset(const std::string& quoteAsset) const {
  std::lock_guard<std::mutex> lock(symbolMutex_);
  std::vector<Symbol> result;
  
  auto it = symbolsByQuoteAsset_.find(quoteAsset);
  if (it != symbolsByQuoteAsset_.end()) {
    for (const auto& symName : it->second) {
      auto symIt = symbols_.find(symName);
      if (symIt != symbols_.end()) {
        result.push_back(symIt->second);
      }
    }
  }
  
  // Sort by quote volume descending
  std::sort(result.begin(), result.end(), [](const Symbol& a, const Symbol& b) {
    return a.quoteVolume24h > b.quoteVolume24h;
  });
  return result;
}

std::vector<Symbol> DataManager::getSymbolsByBaseAsset(const std::string& baseAsset) const {
  std::lock_guard<std::mutex> lock(symbolMutex_);
  std::vector<Symbol> result;
  
  auto it = symbolsByBaseAsset_.find(baseAsset);
  if (it != symbolsByBaseAsset_.end()) {
    for (const auto& symName : it->second) {
      auto symIt = symbols_.find(symName);
      if (symIt != symbols_.end()) {
        result.push_back(symIt->second);
      }
    }
  }
  
  // Sort by quote volume descending
  std::sort(result.begin(), result.end(), [](const Symbol& a, const Symbol& b) {
    return a.quoteVolume24h > b.quoteVolume24h;
  });
  return result;
}

void DataManager::updateSymbolPrice(const std::string& symbolName, double price, double priceChange,
                                   double priceChangePercent, double high24h, double low24h,
                                   double volume24h, double quoteVolume24h) {
  {
    std::lock_guard<std::mutex> lock(symbolMutex_);
    auto it = symbols_.find(symbolName);
    if (it != symbols_.end()) {
      it->second.lastPrice = price;
      it->second.priceChange = priceChange;
      it->second.priceChangePercent = priceChangePercent;
      it->second.high24h = high24h;
      it->second.low24h = low24h;
      it->second.volume24h = volume24h;
      it->second.quoteVolume24h = quoteVolume24h;
      it->second.lastUpdateTime = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
      ).count();
    }
  }
  
  // Update in database
  if (database_) {
    database_->updateSymbolPrice(symbolName, price, priceChange, priceChangePercent, 
                                 high24h, low24h, volume24h, quoteVolume24h);
  }
  
  if (onDataUpdate_) {
    onDataUpdate_();
  }
}

std::vector<std::string> DataManager::getQuoteAssets() const {
  std::lock_guard<std::mutex> lock(symbolMutex_);
  std::vector<std::string> result;
  for (const auto& pair : symbolsByQuoteAsset_) {
    result.push_back(pair.first);
  }
  std::sort(result.begin(), result.end());
  return result;
}

std::vector<std::string> DataManager::getBaseAssets() const {
  std::lock_guard<std::mutex> lock(symbolMutex_);
  std::vector<std::string> result;
  for (const auto& pair : symbolsByBaseAsset_) {
    result.push_back(pair.first);
  }
  std::sort(result.begin(), result.end());
  return result;
}

} // namespace core
} // namespace glora
