# Glora Chart - High-Performance Cryptocurrency Charting Application

A high-performance, low-latency cross-platform desktop application for visualizing cryptocurrency market data. Inspired by TradingView, Glora Chart provides Candlestick, Volume, and Footprint charts with real-time data streaming from Binance.

## Features

- **Real-time Market Data**: WebSocket-based streaming for instant trade updates
- **Multiple Chart Types**:
  - Candlestick (OHLC) charts
  - Volume charts
  - Footprint charts (Bid/Ask volume profile)
- **Historical Data**: Fetch and display 7-10 days of historical aggregated trades
- **User API Integration**: Connect to Binance using your own API keys
- **Configurable History Duration**: Choose how much history to download (3 days, 7 days, etc.)
- **Local Database Storage**: High-performance local SQLite database for history
- **Gap Detection & Filling**: Automatically detect and fill missing data periods
- **Hybrid Data Loading**: Load from local cache + stream live data seamlessly
- **Local Database Storage**: High-performance local SQLite database for history
- **Gap Detection & Filling**: Automatically detect and fill missing data periods
- **Hybrid Data Loading**: Load from local cache + stream live data seamlessly
- **Hardware Accelerated**: OpenGL-based rendering for smooth 60+ FPS visualization
- **Cross-Platform**: Windows and Linux support (Android planned)

## Architecture

The application follows a multi-threaded architecture to achieve low latency:

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│  Network Thread │────▶│ Processing     │────▶│ Render/UI      │
│  (WebSocket)    │     │ Thread          │     │ Thread          │
└─────────────────┘     │ (Lock-free Q)   │     │ (OpenGL/ImGui)  │
                        └─────────────────┘     └─────────────────┘
```

### Core Components

| Module | Description |
|--------|-------------|
| [`src/network/`](src/network/) | Binance API client (REST + WebSocket) |
| [`src/core/`](src/core/) | Data models, threading, database, and utilities |
| [`src/render/`](src/render/) | Chart rendering engine with OpenGL |
| [`src/settings/`](src/settings/) | User settings and API configuration UI |

## Frontend Architecture (Under Reconstruction)

> ⚠️ **The React frontend has been removed as we transition to a new high-performance architecture.**

The frontend is being rebuilt with a modern stack optimized for trading applications:

| Component | Technology | Rationale |
|-----------|------------|-----------|
| **UI Framework** | SolidJS or Svelte 5 | Fine-grained reactivity, no Virtual DOM overhead |
| **Charting Engine** | SciChart / WebGL / WebGPU | Millions of data points at 60 FPS |
| **Communication** | WebSockets + FlatBuffers | Zero-copy deserialization for HF data |
| **Computations** | Wasm (Rust/C++) | Near-native speed for indicators |
| **Parallelism** | OffscreenCanvas | Render off main thread |

### Backend Communication

The C++ backend exposes data via:
- **WebSocket Server**: `ws://localhost:8080` - Real-time market data streaming
- **WebView IPC**: When `USE_WEBVIEW=1` environment variable is set

See [`schema/market_data.fbs`](schema/market_data.fbs) for FlatBuffers schema.

## Data Storage

### SQLite Database

The application uses SQLite for high-performance local storage of historical market data:

- **Tick Data**: Individual trades with timestamp, price, quantity, and side
- **Candle Data**: Aggregated OHLCV data for quick chart loading
- **Footprint Profiles**: Bid/Ask volume at each price level
- **Gap Tracking**: Records of missing time periods for gap filling

### Gap Detection & Filling

When the application starts or after periods of inactivity:

1. **Gap Detection**: Compare local data timestamps with expected time intervals
2. **Gap Analysis**: Identify missing time ranges that need to be downloaded
3. **Background Download**: Fetch missing historical data in chunks
4. **Seamless Integration**: Merge downloaded data with existing local cache
5. **Live Continuation**: Stream new data from WebSocket after gap is filled

## Settings

### API Configuration

Users can configure their own API credentials:

- **API Key**: User's Binance API key
- **API Secret**: User's Binance API secret (stored securely)
- **Testnet Option**: Use Binance Testnet for testing

### History Duration

Configurable download duration:

- Last 3 days
- Last 7 days
- Last 14 days
- Last 30 days
- Custom range

### Data Flow with Settings

```
User Settings (API Key, Duration)
         │
         ▼
┌──────────────────┐     ┌─────────────────┐
│  Check Local DB │────▶│ Load from Cache │
└──────────────────┘     └─────────────────┘
         │                        │
         ▼                        ▼
┌──────────────────┐     ┌─────────────────┐
│  Detect Gaps    │────▶│  Render Chart   │
└──────────────────┘     └─────────────────┘
         │
         ▼
┌──────────────────┐
│ Download Missing │
│    History       │
└──────────────────┘
         │
         ▼
┌──────────────────┐
│ Stream Live Data │
└──────────────────┘
```

## Tech Stack

- **Language**: C++20
- **Build System**: CMake
- **UI Framework**: Dear ImGui with SDL2
- **Graphics**: OpenGL 3
- **Networking**: ixWebSocket (WebSocket client)
- **JSON Parsing**: nlohmann/json
- **Database**: SQLite (via SQLite3)

## Building

### Prerequisites

- CMake 3.16+
- C++20 compatible compiler (GCC 11+, Clang 14+, MSVC 2022)
- SDL2 development libraries
- OpenGL development libraries

### Build Instructions

```bash
# Create build directory
cmake -B build -G Ninja

# Build the project
cmake --build build

# Run the application
./build/GloraChart
```

## Project Structure

```
glora2.0-1/
├── CMakeLists.txt           # Build configuration
├── plan/                    # Architecture and design documents
│   ├── 01_high_level_architecture.md
│   ├── 02_data_acquisition_module.md
│   ├── 03_charting_engine_module.md
│   └── 04_cross_platform_deployment.md
├── src/
│   ├── main.cpp             # Application entry point
│   ├── core/
│   │   ├── DataModels.h     # Core data structures (Tick, Candle, etc.)
│   │   ├── ChartDataManager.h
│   │   └── ThreadSafeQueue.h
│   ├── network/
│   │   ├── BinanceClient.h  # Binance API interface
│   │   └── BinanceClient.cpp
│   └── render/
│       ├── MainWindow.h/cpp # Main application window
│       ├── ChartRenderer.h/cpp
│       └── Camera.h
├── web/                     # Frontend (under reconstruction)
│   └── index.html          # Placeholder for new frontend
└── third_party/             # Dependencies (managed via CMake FetchContent)
```

## Data Models

### Tick
Represents a single trade from the exchange:
- `timestamp_ms`: Trade timestamp (milliseconds)
- `price`: Execution price
- `quantity`: Trade quantity
- `is_buyer_maker`: True if aggressive seller (hit bid), False if aggressive buyer (hit ask)

### Candle
OHLCV data with footprint profile:
- `start_time_ms`, `end_time_ms`: Candle timeframe
- `open`, `high`, `low`, `close`: OHLC prices
- `volume`: Total traded volume
- `footprint_profile`: Bid/Ask volume at each price level

## Supported Chart Types

1. **Candlestick Chart**: Traditional OHLC visualization with instanced rendering
2. **Volume Chart**: Volume bars colored by bullish/bearish movement
3. **Footprint Chart**: Detailed bid/ask volume at each price level

## Future Enhancements

### In Progress

- **User Settings UI**: Settings page for API key configuration
- **SQLite Database**: High-performance local storage for historical data
- **Gap Detection & Filling**: Automatic missing data detection and download
- **Hybrid Data Loading**: Seamless combination of local cache and live streaming

### Planned

- Android mobile support via Qt 6
- Additional chart types (Heikin-Ashi, Renko)
- Multiple timeframe support
- Technical indicators

## License

MIT License

## Acknowledgments

- [Dear ImGui](https://github.com/ocornut/imgui) - Bloat-free immediate mode GUI
- [ixWebSocket](https://github.com/machinezone/IXWebSocket) - High-performance WebSocket library
- [nlohmann/json](https://github.com/nlohmann/json) - JSON for Modern C++
