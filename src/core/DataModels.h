#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>

namespace glora {
namespace core {

// Custom flat_map for better cache locality - similar to std::flat_map in C++23
// Uses sorted vectors instead of tree nodes for better memory layout
template<typename Key, typename Value, typename Compare = std::greater<Key>>
class flat_map {
private:
    std::vector<std::pair<Key, Value>> data_;
    Compare comp_;
    
    void sort() {
        std::sort(data_.begin(), data_.end(), 
            [this](const auto& a, const auto& b) {
                return comp_(a.first, b.first);
            });
    }
    
public:
    using iterator = typename std::vector<std::pair<Key, Value>>::iterator;
    using const_iterator = typename std::vector<std::pair<Key, Value>>::const_iterator;
    
    flat_map() = default;
    
    Value& operator[](const Key& key) {
        // Find existing key
        for (auto& pair : data_) {
            if (!comp_(key, pair.first) && !comp_(pair.first, key)) {
                return pair.second;
            }
        }
        // Insert new key
        data_.emplace_back(key, Value{});
        sort();
        // Return reference to newly inserted
        for (auto& pair : data_) {
            if (!comp_(key, pair.first) && !comp_(pair.first, key)) {
                return pair.second;
            }
        }
        return data_.back().second; // Fallback
    }
    
    const Value& operator[](const Key& key) const {
        static Value empty;
        for (const auto& pair : data_) {
            if (!comp_(key, pair.first) && !comp_(pair.first, key)) {
                return pair.second;
            }
        }
        return empty;
    }
    
    iterator find(const Key& key) {
        for (auto it = data_.begin(); it != data_.end(); ++it) {
            if (!comp_(key, it->first) && !comp_(it->first, key)) {
                return it;
            }
        }
        return data_.end();
    }
    
    const_iterator find(const Key& key) const {
        for (auto it = data_.begin(); it != data_.end(); ++it) {
            if (!comp_(key, it->first) && !comp_(it->first, key)) {
                return it;
            }
        }
        return data_.end();
    }
    
    void erase(const Key& key) {
        data_.erase(
            std::remove_if(data_.begin(), data_.end(),
                [this, &key](const auto& pair) {
                    return !comp_(key, pair.first) && !comp_(pair.first, key);
                }),
            data_.end()
        );
    }
    
    size_t size() const { return data_.size(); }
    bool empty() const { return data_.empty(); }
    
    iterator begin() { return data_.begin(); }
    iterator end() { return data_.end(); }
    const_iterator begin() const { return data_.begin(); }
    const_iterator end() const { return data_.end(); }
    
    void clear() { data_.clear(); }
};

// Represents a single trade from the exchange
struct Tick {
  uint64_t timestamp_ms; // Unix timestamp in milliseconds
  double price;          // Execution price
  double quantity;       // Execution quantity
  bool is_buyer_maker;   // Determines if the trade was an active SELL (true) or
                         // active BUY (false)
};

// Symbol metadata from exchange info
struct Symbol {
  std::string symbol;           // e.g. "BTCUSDT"
  std::string baseAsset;        // e.g. "BTC"
  std::string quoteAsset;       // e.g. "USDT"
  std::string status;          // e.g. "TRADING"
  std::string permissions;      // e.g. "SPOT"
  double minPrice = 0.0;
  double maxPrice = 0.0;
  double tickSize = 0.0;
  double minQty = 0.0;
  double maxQty = 0.0;
  double stepSize = 0.0;
  double minNotional = 0.0;
  
  // Real-time price data
  double lastPrice = 0.0;
  double priceChange = 0.0;
  double priceChangePercent = 0.0;
  double high24h = 0.0;
  double low24h = 0.0;
  double volume24h = 0.0;
  double quoteVolume24h = 0.0;
  uint64_t lastUpdateTime = 0;
  
  // Filter helpers
  bool isTrading() const { return status == "TRADING"; }
  bool isSpot() const { return permissions.find("SPOT") != std::string::npos; }
};

// Tracks Bid and Ask volume at a specific price level for the Footprint
struct PriceNode {
  double bid_volume = 0.0; // Selling volume hitting Bids
  double ask_volume = 0.0; // Buying volume hitting Asks
};

// A single candlestick containing OHLCV and Footprint profile
struct Candle {
  uint64_t start_time_ms; // Interval start time
  uint64_t end_time_ms;   // Interval end time

  double open = 0.0;
  double high = 0.0;
  double low = 0.0;
  double close = 0.0;
  double volume = 0.0;

  // Footprint Profile: Price -> [Bid Vol, Ask Vol]
  // Using flat_map (sorted vector) instead of std::map for better cache locality
  // This provides O(n) lookup but with much better memory access patterns
  flat_map<double, PriceNode, std::greater<double>> footprint_profile;

  void add_tick(const Tick &tick) {
    // Update OHLC
    if (open == 0.0) {
      open = high = low = close = tick.price;
    } else {
      if (tick.price > high)
        high = tick.price;
      if (tick.price < low)
        low = tick.price;
      close = tick.price;
    }
    volume += tick.quantity;

    // Update Footprint
    if (tick.is_buyer_maker) {
      // Aggressor was a seller (hit the bid)
      footprint_profile[tick.price].bid_volume += tick.quantity;
    } else {
      // Aggressor was a buyer (hit the ask)
      footprint_profile[tick.price].ask_volume += tick.quantity;
    }
  }
};

// Holds the historical and current series of candles for a symbol
struct SymbolData {
  std::string symbol; // e.g. "BTCUSDT"
  std::vector<Candle> candles;
};

} // namespace core
} // namespace glora
