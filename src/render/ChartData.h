#pragma once

#include "../core/DataModels.h"
#include <vector>
#include <optional>
#include <limits>
#include <cmath>
#include <algorithm>

namespace glora {
namespace render {

// Chart data wrapper for rendering
class ChartData {
public:
  ChartData() = default;
  ~ChartData() = default;
  
  // Set candle data
  void setCandles(const std::vector<core::Candle>& candles);
  
  // Get candles in visible range
  std::vector<core::Candle> getVisibleCandles(uint64_t startTime, uint64_t endTime) const;
  
  // Get all candles
  const std::vector<core::Candle>& getAllCandles() const { return candles_; }
  
  // Get price range
  std::pair<double, double> getPriceRange() const;
  
  // Get time range
  std::pair<uint64_t, uint64_t> getTimeRange() const;
  
  // Get base price (for percentage/indexed scales)
  double getBasePrice() const;
  
  // Find nearest price level
  double findNearestPriceLevel(double price, double tolerance = 0.01) const;
  
  // Find nearest time
  uint64_t findNearestTime(uint64_t time) const;
  
  // Find nearest OHLC to position
  std::optional<double> findNearestOHLC(uint64_t time, double price) const;

private:
  std::vector<core::Candle> candles_;
};

inline void ChartData::setCandles(const std::vector<core::Candle>& candles) {
  candles_ = candles;
}

inline std::vector<core::Candle> ChartData::getVisibleCandles(uint64_t startTime, uint64_t endTime) const {
  std::vector<core::Candle> visible;
  for (const auto& candle : candles_) {
    if (candle.start_time_ms >= startTime && candle.start_time_ms <= endTime) {
      visible.push_back(candle);
    }
  }
  return visible;
}

inline std::pair<double, double> ChartData::getPriceRange() const {
  if (candles_.empty()) {
    return {0, 100};
  }
  
  double minPrice = candles_[0].low;
  double maxPrice = candles_[0].high;
  
  for (const auto& candle : candles_) {
    if (candle.low < minPrice) minPrice = candle.low;
    if (candle.high > maxPrice) maxPrice = candle.high;
  }
  
  return {minPrice, maxPrice};
}

inline std::pair<uint64_t, uint64_t> ChartData::getTimeRange() const {
  if (candles_.empty()) {
    return {0, 0};
  }
  
  return {candles_.front().start_time_ms, candles_.back().end_time_ms};
}

inline double ChartData::getBasePrice() const {
  if (candles_.empty()) return 0;
  return candles_.front().open; // First candle's open as base
}

inline double ChartData::findNearestPriceLevel(double price, double tolerance) const {
  if (candles_.empty()) return price;
  
  double nearest = price;
  double minDiff = tolerance * price; // 1% tolerance by default
  
  for (const auto& candle : candles_) {
    // Check all price points
    double prices[] = {candle.open, candle.high, candle.low, candle.close};
    for (double p : prices) {
      double diff = std::abs(p - price);
      if (diff < minDiff) {
        minDiff = diff;
        nearest = p;
      }
    }
  }
  
  return nearest;
}

inline uint64_t ChartData::findNearestTime(uint64_t time) const {
  if (candles_.empty()) return time;
  
  uint64_t nearest = candles_[0].start_time_ms;
  uint64_t minDiff = static_cast<uint64_t>(std::abs(static_cast<int64_t>(time) - static_cast<int64_t>(nearest)));
  
  for (const auto& candle : candles_) {
    uint64_t t = candle.start_time_ms;
    uint64_t diff = static_cast<uint64_t>(std::abs(static_cast<int64_t>(time) - static_cast<int64_t>(t)));
    if (diff < minDiff) {
      minDiff = diff;
      nearest = t;
    }
  }
  
  return nearest;
}

inline std::optional<double> ChartData::findNearestOHLC(uint64_t time, double price) const {
  if (candles_.empty()) return std::nullopt;
  
  // Find nearest candle by time
  const core::Candle* nearestCandle = nullptr;
  uint64_t minTimeDiff = std::numeric_limits<uint64_t>::max();
  
  for (const auto& candle : candles_) {
    uint64_t diff = static_cast<uint64_t>(std::abs(static_cast<int64_t>(time) - static_cast<int64_t>(candle.start_time_ms)));
    if (diff < minTimeDiff) {
      minTimeDiff = diff;
      nearestCandle = &candle;
    }
  }
  
  if (!nearestCandle) return std::nullopt;
  
  // Find nearest OHLC price
  double prices[] = {
    nearestCandle->open,
    nearestCandle->high,
    nearestCandle->low,
    nearestCandle->close
  };
  
  double nearest = prices[0];
  double minDiff = std::abs(prices[0] - price);
  
  for (int i = 1; i < 4; i++) {
    double diff = std::abs(prices[i] - price);
    if (diff < minDiff) {
      minDiff = diff;
      nearest = prices[i];
    }
  }
  
  return nearest;
}

} // namespace render
} // namespace glora
