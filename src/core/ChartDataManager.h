#pragma once

#include "DataModels.h"
#include <array>
#include <chrono>
#include <cmath>
#include <mutex>

namespace glora {
namespace core {

// Timeframe intervals in milliseconds
enum class Timeframe {
  M1 = 60 * 1000,         // 1 minute
  M5 = 5 * 60 * 1000,     // 5 minutes
  M15 = 15 * 60 * 1000,   // 15 minutes
  M30 = 30 * 60 * 1000,   // 30 minutes
  H1 = 60 * 60 * 1000,     // 1 hour
  H4 = 4 * 60 * 60 * 1000, // 4 hours
  D1 = 24 * 60 * 60 * 1000 // 1 day
};

class ChartDataManager {
public:
  ChartDataManager(Timeframe timeframe = Timeframe::M1)
      : timeframe_(static_cast<uint64_t>(timeframe)) {}

  // Add a tick and return the current candle
  void addTick(const Tick &tick);

  // Get all candles
  const std::vector<Candle> &getCandles() const { return candles_; }

  // Get the current (in-progress) candle
  const Candle &getCurrentCandle() const { return currentCandle_; }

  // Set timeframe
  void setTimeframe(Timeframe timeframe) {
    timeframe_ = static_cast<uint64_t>(timeframe);
    // Reset candles when timeframe changes
    candles_.clear();
    currentCandle_ = Candle();
  }

  // Get price range for Y-axis
  std::pair<double, double> getPriceRange() const;

  // Get time range for X-axis
  std::pair<uint64_t, uint64_t> getTimeRange() const;

  // Initialize with historical data
  void setHistoricalData(const std::vector<Tick> &ticks);

private:
  uint64_t timeframe_;
  std::vector<Candle> candles_;
  Candle currentCandle_;
  mutable std::mutex mutex_;
};

inline void ChartDataManager::addTick(const Tick &tick) {
  std::lock_guard<std::mutex> lock(mutex_);

  uint64_t candleStartTime =
      (tick.timestamp_ms / timeframe_) * timeframe_;
  uint64_t candleEndTime = candleStartTime + timeframe_;

  // Check if we need to start a new candle
  if (currentCandle_.start_time_ms == 0) {
    // First tick
    currentCandle_.start_time_ms = candleStartTime;
    currentCandle_.end_time_ms = candleEndTime;
    currentCandle_.add_tick(tick);
  } else if (candleStartTime > currentCandle_.start_time_ms) {
    // Completed candle - save it
    if (currentCandle_.volume > 0) {
      candles_.push_back(currentCandle_);
    }
    // Start new candle
    currentCandle_ = Candle();
    currentCandle_.start_time_ms = candleStartTime;
    currentCandle_.end_time_ms = candleEndTime;
    currentCandle_.add_tick(tick);
  } else {
    // Still in same candle - update it
    currentCandle_.add_tick(tick);
  }
}

inline void ChartDataManager::setHistoricalData(const std::vector<Tick> &ticks) {
  std::lock_guard<std::mutex> lock(mutex_);
  candles_.clear();
  currentCandle_ = Candle();

  for (const auto &tick : ticks) {
    addTick(tick);
  }
}

inline std::pair<double, double> ChartDataManager::getPriceRange() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (candles_.empty() && currentCandle_.volume == 0) {
    return {0.0, 0.0};
  }

  double minPrice = std::numeric_limits<double>::max();
  double maxPrice = std::numeric_limits<double>::lowest();

  for (const auto &candle : candles_) {
    if (candle.low < minPrice)
      minPrice = candle.low;
    if (candle.high > maxPrice)
      maxPrice = candle.high;
  }

  // Include current candle
  if (currentCandle_.volume > 0) {
    if (currentCandle_.low < minPrice)
      minPrice = currentCandle_.low;
    if (currentCandle_.high > maxPrice)
      maxPrice = currentCandle_.high;
  }

  // Add some padding
  double range = maxPrice - minPrice;
  return {minPrice - range * 0.05, maxPrice + range * 0.05};
}

inline std::pair<uint64_t, uint64_t> ChartDataManager::getTimeRange() const {
  std::lock_guard<std::mutex> lock(mutex_);

  if (candles_.empty()) {
    return {0, 0};
  }

  return {candles_.front().start_time_ms,
          currentCandle_.volume > 0 ? currentCandle_.end_time_ms
                                   : candles_.back().end_time_ms};
}

} // namespace core
} // namespace glora
