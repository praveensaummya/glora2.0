# Data Acquisition Module

## Overview

This module connects to cryptocurrency exchanges (primary target: Binance) to acquire both historical data (to populate the charts initially) and real-time data for sub-millisecond updates.

## Historical Data (REST API)

To display a detailed Footprint chart for 7-10 days, we cannot rely solely on standard Candlestick (Kline) data. Standard Klines do not contain bid/ask split volume at specific price levels.

* **Fetch Strategy:**
  * Fetch standard Kline/Candlestick data via Binance REST API for the basic chart shape.
  * Fetch **Aggregated Trades (AggTrades)** or **Historical Trades** for the past 7-10 days to reconstruct the Footprint.
  * *Note:* 7-10 days of raw tick data is massive. The app should download this in chunks asynchronously and cache it locally (e.g., in a local SQLite database or custom binary format) to avoid redownloading on every startup.

## Real-Time Data (WebSockets)

For a low-latency trading experience, WebSockets are mandatory.

* **Streams to Subscribe:**
  * `@kline_<interval>`: For real-time updates to the current active candlestick.
  * `@trade` or `@aggTrade`: For real-time order matching data. This is crucial for updating the real-time Footprint chart (adding volume to the specific bid or ask price level).
  * `@depth` (Optional): Order book updates if you intend to show Market Depth/DOM alongside the charts.

## Implementation Details

1. **Connection Handling:** Implement auto-reconnect logic with exponential backoff.
2. **Latency Optimization:**
    * Utilize zero-copy parsing techniques where possible.
    * Disable Nagle's algorithm (`TCP_NODELAY`) on sockets.
3. **Data Structures:**
    * Use Cache-Line friendly data structures.
    * Represent prices using integers/fixed-point math (e.g., multiplying by 10^8) to avoid floating-point inaccuracies and speed up calculations.

## Example Flow

1. User selects "BTCUSDT".
2. App sends REST requests for 7 days of historical AggTrades.
3. App simultaneously opens a WebSocket connection to listen for new AggTrades.
4. Historical data is processed and sent to the renderer.
5. Incoming WebSocket trades are appended to the most recent candle and footprint profile.
