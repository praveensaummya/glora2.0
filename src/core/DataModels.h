#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace glora {
namespace core {

// Represents a single trade from the exchange
struct Tick {
  uint64_t timestamp_ms; // Unix timestamp in milliseconds
  double price;          // Execution price
  double quantity;       // Execution quantity
  bool is_buyer_maker;   // Determines if the trade was an active SELL (true) or
                         // active BUY (false)
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
  // Often exchanges provide prices that aren't perfectly aligned to ticks.
  // In a real high-perf app, this map would be an array mapped by
  // `price_ticks`. Using std::map for simplicity here, descending order.
  std::map<double, PriceNode, std::greater<double>> footprint_profile;

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
