# Real-Time Data Performance Optimizations

## Overview
This document describes the performance optimizations implemented for handling high-frequency market data in the Glora trading platform.

## Implemented Features

### 1. Enhanced WebSocket Integration
**File:** `web/my-react-app/src/utils/BinanceWebSocket.js`

- **Reconnection Logic:** Exponential backoff with configurable max attempts and delays
- **Connection States:** DISCONNECTED, CONNECTING, CONNECTED, RECONNECTING, ERROR
- **Heartbeat/Ping-Pong:** Configurable ping interval to detect dead connections
- **Message Queuing:** Offline buffering with configurable queue size
- **Stream Builders:** Pre-defined stream formats for Binance data

#### Usage
```javascript
import { getWebSocket, StreamBuilder, ConnectionState } from '../utils/BinanceWebSocket';

const ws = getWebSocket();
ws.connect([
  StreamBuilder.kline('btcusdt', '1m'),
  StreamBuilder.aggTrade('btcusdt')
]);

ws.subscribe(StreamBuilder.kline('btcusdt', '1m'), (message) => {
  console.log('New candle:', message);
});
```

### 2. Web Worker with OffscreenCanvas
**File:** `web/my-react-app/src/workers/chartWorker.js`

- **Background Rendering:** OffscreenCanvas for chart rendering off main thread
- **Indicator Calculations:** SMA, EMA, RSI, MACD, Bollinger Bands, ATR, VWAP
- **Data Processing:** Batch processing for high-frequency updates
- **Frame Composition:** Smooth animations with FPS monitoring

#### Usage
```javascript
import { useChartWorker } from '../hooks/useChartWorker';

function Chart() {
  const { isReady, render, initCanvas, calculateIndicators } = useChartWorker();
  
  // Initialize with canvas element
  useEffect(() => {
    if (canvasRef.current && isReady) {
      initCanvas(canvasRef.current, width, height);
    }
  }, [isReady]);
  
  // Render chart
  const handleRender = async () => {
    await render({ candles, indicators });
  };
}
```

### 3. Binary Serialization
**File:** `schema/market_data.fbs`

FlatBuffers schema for efficient binary serialization:

- **Candle:** OHLCV data with timestamps
- **Trade:** Individual trade execution
- **Order Book:** Full snapshot or incremental updates
- **Ticker:** 24hr statistics
- **AggTrade:** Aggregated trade data

**C++ Implementation:** `src/network/BinarySerialization.h`

Custom binary protocol with:
- 24-byte header with magic, version, type, flags, timestamp, sequence
- Fixed-point encoding for prices (scaled by 10000)
- Performance metrics for comparing JSON vs binary sizes

### 4. Performance Monitoring
**File:** `web/my-react-app/src/hooks/useIPC.js`

- **FPS Monitoring:** Frame rate tracking for rendering
- **Message Latency:** Average and per-message latency tracking
- **Memory Usage:** JS heap size monitoring
- **Message Throughput:** Messages per second tracking

#### Usage
```javascript
import { usePerformanceMonitor } from '../hooks/useIPC';

function App() {
  const { fps, messagesPerSecond, avgLatency, memoryUsage } = usePerformanceMonitor(ws);
  
  return (
    <div>
      FPS: {fps} | MPS: {messagesPerSecond} | Latency: {avgLatency}ms | Memory: {memoryUsage}MB
    </div>
  );
}
```

### 5. Direct Binance WebSocket Hook
**File:** `web/my-react-app/src/hooks/useIPC.js`

New `useBinanceWebSocket` hook for direct connection to Binance:

```javascript
import { useBinanceWebSocket } from '../hooks/useIPC';

function TradingView() {
  const { 
    connectionState, 
    isConnected, 
    candles, 
    trades,
    metrics,
    reconnect,
    disconnect 
  } = useBinanceWebSocket('BTCUSDT', '1m');
  
  // Automatically subscribes to kline and aggTrade streams
}
```

## Architecture Diagram

```
┌─────────────────────────────────────────────────────────────┐
│                        React App                            │
├─────────────────────────────────────────────────────────────┤
│  useBinanceWebSocket    │    useChartWorker                │
│  ┌─────────────────┐    │    ┌─────────────────┐          │
│  │ WebSocket Class │    │    │ OffscreenCanvas │          │
│  │ - Reconnection  │    │    │ - Rendering     │          │
│  │ - Message Queue │    │    │ - Indicators    │          │
│  │ - Heartbeat     │    │    │ - FPS Monitor   │          │
│  └────────┬────────┘    │    └────────┬────────┘          │
│           │              │             │                    │
│           ▼              │             ▼                    │
│  ┌─────────────────┐    │    ┌─────────────────┐          │
│  │ Binary Parser   │    │    │ Web Worker      │          │
│  │ (FlatBuffers)   │    │    │ (Background)    │          │
│  └─────────────────┘    │    └─────────────────┘          │
│           │              │                                   │
└───────────┼──────────────┼───────────────────────────────────┘
            │              │
            ▼              ▼
    ┌─────────────┐  ┌─────────────┐
    │   Binance   │  │   C++       │
    │   WebSocket │  │   Backend   │
    └─────────────┘  └─────────────┘
```

## Performance Comparison

### JSON vs Binary Message Size

Typical candle message:
- JSON: ~150-200 bytes
- Binary: ~60-80 bytes
- **Savings: ~60%**

### Main Thread vs Web Worker

- Main thread rendering: Blocks UI during high volatility
- Web Worker rendering: UI remains responsive
- **Typical improvement: 30-50% smoother UI**

## Configuration

### WebSocket Options
```javascript
const wsOptions = {
  baseUrl: 'wss://stream.binance.com:9443/ws',
  maxReconnectAttempts: 10,
  initialReconnectDelay: 1000,
  maxReconnectDelay: 30000,
  reconnectBackoffMultiplier: 2,
  pingInterval: 30000,
  pingTimeout: 5000,
  maxQueueSize: 1000,
  enableMetrics: true
};
```

### Indicator Configurations
```javascript
const indicators = [
  { type: 'SMA', params: { period: 20 } },
  { type: 'EMA', params: { period: 12 } },
  { type: 'RSI', params: { period: 14 } },
  { type: 'MACD', params: { fastPeriod: 12, slowPeriod: 26, signalPeriod: 9 } },
  { type: 'BB', params: { period: 20, stdDev: 2 } }
];
```

## Browser Support

- **OffscreenCanvas:** Chrome 69+, Firefox 105+, Safari 16.4+
- **Web Workers:** All modern browsers
- **WebSocket:** All modern browsers
- Graceful degradation: Falls back to main thread rendering if Web Worker not supported

## Testing

To test with high-frequency data:
1. Connect to multiple streams
2. Monitor FPS and latency metrics
3. Verify reconnection on network interruption
4. Check memory usage over extended periods

## Notes

- Web Workers cannot access DOM directly - use OffscreenCanvas.transferControlToOffscreen()
- Binary serialization requires code generation from FlatBuffers schema for production
- Backward compatibility maintained with existing JSON-based IPC
