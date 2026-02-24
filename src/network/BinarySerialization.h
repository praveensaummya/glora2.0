/**
 * BinarySerialization.h - Binary Message Protocol for Market Data
 * 
 * This provides a simple binary serialization format that's more efficient
 * than JSON for high-frequency market data.
 * 
 * Binary Message Format:
 * - Header (8 bytes): magic + version + message type + flags
 * - Payload: message-specific binary data
 * 
 * Message Types:
 * - 0x01: Candle (OHLCV)
 * - 0x02: Trade
 * - 0x03: Order Book
 * - 0x04: Ticker
 * 
 * Note: For full FlatBuffers support, use flatc to generate code from market_data.fbs
 * This header provides a lightweight fallback with custom binary format.
 */

#ifndef GLORA_BINARY_SERIALIZATION_H
#define GLORA_BINARY_SERIALIZATION_H

#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include <memory>
#include <functional>
#include <chrono>
#include <cmath>
#include <algorithm>

namespace glora {
namespace network {

// Binary message constants
static const uint32_t BINARY_MAGIC = 0x474C5244; // "GLRD"
static const uint8_t  BINARY_VERSION = 1;

// Message types
enum class BinaryMessageType : uint8_t {
  Unknown = 0x00,
  Candle = 0x01,
  Trade = 0x02,
  OrderBook = 0x03,
  OrderBookUpdate = 0x04,
  Ticker = 0x05,
  AggTrade = 0x06
};

// Binary message flags
enum class BinaryFlags : uint8_t {
  None = 0x00,
  Compressed = 0x01,
  Encrypted = 0x02,
  Final = 0x04
};

// Binary message header
#pragma pack(push, 1)
struct BinaryHeader {
  uint32_t magic;        // Magic number "GLRD"
  uint8_t  version;      // Protocol version
  uint8_t  type;         // Message type
  uint8_t  flags;        // Message flags
  uint8_t  reserved;     // Reserved for future use
  uint32_t payloadSize;  // Size of payload
  uint64_t timestamp;    // Message timestamp (ms since epoch)
  uint64_t sequence;    // Sequence number
};
#pragma pack(pop)

static_assert(sizeof(BinaryHeader) == 24, "BinaryHeader must be 24 bytes");

// Candle binary format
struct BinaryCandle {
  uint64_t openTime;     // Open time (ms)
  uint64_t closeTime;    // Close time (ms)
  int64_t  openPrice;   // Open price (scaled by 10000)
  int64_t  highPrice;    // High price (scaled by 10000)
  int64_t  lowPrice;     // Low price (scaled by 10000)
  int64_t  closePrice;   // Close price (scaled by 10000)
  int64_t  volume;        // Volume (scaled by 1000000)
  int64_t  quoteVolume;  // Quote volume (scaled by 1000000)
  uint32_t trades;       // Number of trades
  uint8_t  closed;       // Is candle closed (1 = yes)
};

// Trade binary format
struct BinaryTrade {
  int64_t  tradeId;      // Trade ID
  int64_t  price;        // Price (scaled by 10000)
  int64_t  quantity;     // Quantity (scaled by 1000000)
  int64_t  quoteQuantity; // Quote quantity (scaled by 1000000)
  uint64_t tradeTime;    // Trade timestamp (ms)
  uint8_t  side;         // 0 = buy, 1 = sell
};

// Order book entry
struct BinaryOrderBookEntry {
  int64_t price;     // Price (scaled by 10000)
  int64_t quantity;  // Quantity (scaled by 1000000)
};

// Order book binary format
struct BinaryOrderBook {
  uint64_t lastUpdateId;  // Last update ID
  uint16_t bidsCount;     // Number of bids
  uint16_t asksCount;     // Number of asks
  // Followed by bidsCount + asksCount BinaryOrderBookEntry structs
};

// Binary serializer class
class BinarySerializer {
public:
  using Clock = std::chrono::steady_clock;
  
  BinarySerializer() : sequence_(0) {}
  
  // Serialize candle to binary
  std::vector<uint8_t> serializeCandle(
    uint64_t openTime,
    uint64_t closeTime,
    double openPrice,
    double highPrice,
    double lowPrice,
    double closePrice,
    double volume,
    uint32_t trades,
    bool closed
  ) {
    auto start = Clock::now();
    
    BinaryCandle candle;
    candle.openTime = openTime;
    candle.closeTime = closeTime;
    candle.openPrice = doubleToFixed(openPrice);
    candle.highPrice = doubleToFixed(highPrice);
    candle.lowPrice = doubleToFixed(lowPrice);
    candle.closePrice = doubleToFixed(closePrice);
    candle.volume = doubleToFixed(volume);
    candle.quoteVolume = doubleToFixed(openPrice * volume);
    candle.trades = trades;
    candle.closed = closed ? 1 : 0;
    
    return buildMessage(BinaryMessageType::Candle, &candle, sizeof(candle));
  }
  
  // Serialize trade to binary
  std::vector<uint8_t> serializeTrade(
    int64_t tradeId,
    double price,
    double quantity,
    uint64_t tradeTime,
    bool isBuyerMaker
  ) {
    BinaryTrade trade;
    trade.tradeId = tradeId;
    trade.price = doubleToFixed(price);
    trade.quantity = doubleToFixed(quantity);
    trade.quoteQuantity = doubleToFixed(price * quantity);
    trade.tradeTime = tradeTime;
    trade.side = isBuyerMaker ? 1 : 0;
    
    return buildMessage(BinaryMessageType::Trade, &trade, sizeof(trade));
  }
  
  // Serialize order book to binary
  std::vector<uint8_t> serializeOrderBook(
    uint64_t lastUpdateId,
    const std::vector<std::pair<double, double>>& bids,
    const std::vector<std::pair<double, double>>& asks
  ) {
    // Calculate size needed
    const size_t headerSize = sizeof(uint64_t) + sizeof(uint16_t) + sizeof(uint16_t);
    const size_t entrySize = sizeof(BinaryOrderBookEntry);
    const size_t totalSize = headerSize + (bids.size() + asks.size()) * entrySize;
    
    std::vector<uint8_t> buffer;
    buffer.resize(totalSize);
    
    // Write header
    uint64_t* pLastUpdateId = reinterpret_cast<uint64_t*>(buffer.data());
    *pLastUpdateId = lastUpdateId;
    
    uint16_t* pBidsCount = reinterpret_cast<uint16_t*>(buffer.data() + sizeof(uint64_t));
    *pBidsCount = static_cast<uint16_t>(bids.size());
    
    uint16_t* pAsksCount = reinterpret_cast<uint16_t*>(buffer.data() + sizeof(uint64_t) + sizeof(uint16_t));
    *pAsksCount = static_cast<uint16_t>(asks.size());
    
    // Write entries
    uint8_t* entries = buffer.data() + headerSize;
    
    for (size_t i = 0; i < bids.size(); ++i) {
      BinaryOrderBookEntry* entry = reinterpret_cast<BinaryOrderBookEntry*>(entries + i * entrySize);
      entry->price = doubleToFixed(bids[i].first);
      entry->quantity = doubleToFixed(bids[i].second);
    }
    
    for (size_t i = 0; i < asks.size(); ++i) {
      BinaryOrderBookEntry* entry = reinterpret_cast<BinaryOrderBookEntry*>(entries + (bids.size() + i) * entrySize);
      entry->price = doubleToFixed(asks[i].first);
      entry->quantity = doubleToFixed(asks[i].second);
    }
    
    return buildMessage(BinaryMessageType::OrderBook, buffer.data(), buffer.size());
  }
  
  // Deserialize binary message
  struct ParsedMessage {
    BinaryMessageType type;
    uint64_t timestamp;
    uint64_t sequence;
    std::vector<uint8_t> payload;
  };
  
  ParsedMessage parseMessage(const std::vector<uint8_t>& data) {
    ParsedMessage result;
    result.type = BinaryMessageType::Unknown;
    
    if (data.size() < sizeof(BinaryHeader)) {
      return result;
    }
    
    const BinaryHeader* header = reinterpret_cast<const BinaryHeader*>(data.data());
    
    // Validate magic
    if (header->magic != BINARY_MAGIC) {
      return result;
    }
    
    // Validate version
    if (header->version != BINARY_VERSION) {
      return result;
    }
    
    result.type = static_cast<BinaryMessageType>(header->type);
    result.timestamp = header->timestamp;
    result.sequence = header->sequence;
    
    // Copy payload
    const size_t payloadOffset = sizeof(BinaryHeader);
    if (data.size() > payloadOffset) {
      result.payload.resize(data.size() - payloadOffset);
      std::memcpy(result.payload.data(), data.data() + payloadOffset, result.payload.size());
    }
    
    return result;
  }
  
  // Extract candle from payload
  bool extractCandle(const std::vector<uint8_t>& payload, 
    uint64_t& openTime, uint64_t& closeTime,
    double& openPrice, double& highPrice, double& lowPrice, 
    double& closePrice, double& volume, uint32_t& trades, bool& closed) 
  {
    if (payload.size() < sizeof(BinaryCandle)) {
      return false;
    }
    
    const BinaryCandle* candle = reinterpret_cast<const BinaryCandle*>(payload.data());
    
    openTime = candle->openTime;
    closeTime = candle->closeTime;
    openPrice = fixedToDouble(candle->openPrice);
    highPrice = fixedToDouble(candle->highPrice);
    lowPrice = fixedToDouble(candle->lowPrice);
    closePrice = fixedToDouble(candle->closePrice);
    volume = fixedToDouble(candle->volume);
    trades = candle->trades;
    closed = candle->closed != 0;
    
    return true;
  }
  
  // Performance metrics
  struct Metrics {
    uint64_t messagesSerialized;
    uint64_t messagesDeserialized;
    uint64_t totalBytesIn;
    uint64_t totalBytesOut;
    double avgSerializeTimeUs;
    double avgDeserializeTimeUs;
  };
  
  Metrics getMetrics() const {
    return metrics_;
  }
  
  void resetMetrics() {
    metrics_ = Metrics{};
  }

private:
  uint64_t sequence_;
  Metrics metrics_;
  
  // Convert double to fixed-point (scaled by 10000 for price, 1000000 for quantity)
  static int64_t doubleToFixed(double value) {
    return static_cast<int64_t>(std::lround(value * 10000.0));
  }
  
  // Convert fixed-point to double
  static double fixedToDouble(int64_t value) {
    return static_cast<double>(value) / 10000.0;
  }
  
  // Build complete binary message
  std::vector<uint8_t> buildMessage(BinaryMessageType type, const void* payload, size_t payloadSize) {
    std::vector<uint8_t> message;
    message.resize(sizeof(BinaryHeader) + payloadSize);
    
    BinaryHeader* header = reinterpret_cast<BinaryHeader*>(message.data());
    header->magic = BINARY_MAGIC;
    header->version = BINARY_VERSION;
    header->type = static_cast<uint8_t>(type);
    header->flags = static_cast<uint8_t>(BinaryFlags::None);
    header->reserved = 0;
    header->payloadSize = static_cast<uint32_t>(payloadSize);
    header->timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()
    ).count();
    header->sequence = ++sequence_;
    
    std::memcpy(message.data() + sizeof(BinaryHeader), payload, payloadSize);
    
    metrics_.messagesSerialized++;
    metrics_.totalBytesOut += message.size();
    
    return message;
  }
};

// Comparison: JSON vs Binary message sizes
struct SizeComparison {
  size_t jsonSize;
  size_t binarySize;
  double compressionRatio;
  
  static SizeComparison compare(const std::string& json, const std::vector<uint8_t>& binary) {
    SizeComparison result;
    result.jsonSize = json.size();
    result.binarySize = binary.size();
    result.compressionRatio = static_cast<double>(binary.size()) / json.size();
    return result;
  }
};

} // namespace network
} // namespace glora

#endif // GLORA_BINARY_SERIALIZATION_H
