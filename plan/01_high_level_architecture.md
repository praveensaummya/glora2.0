# High-Level Architecture

## Overview
The goal is to develop a high-performance, low-latency cross-platform application (Windows, Linux, Android) that provides Candlestick, Volume, and Footprint charts, similar to TradingView. The application will fetch 7-10 days of historical data from cryptocurrency APIs (e.g., Binance) and stream real-time updates.

## Core Technologies
*   **Language:** C++20 for maximum performance and memory control.
*   **Build System:** CMake to manage the cross-platform builds for Windows (MSVC), Linux (GCC/Clang), and Android (NDK).
*   **User Interface:** **Qt 6** (C++ and QML) is highly recommended. It provides excellent cross-platform support out-of-the-box (including Android) and allows for hardware-accelerated rendering using its RHI (Rendering Hardware Interface) which supports Vulkan, Metal, and Direct3D. Alternatively, **Dear ImGui** with SDL2/GLFW could be used for desktop, but Android support is more complex compared to Qt.
*   **Networking:** `Boost.Asio` or `uWebSockets` for high-performance, low-latency WebSocket and REST connections.
*   **JSON Parsing:** `simdjson` (for ultra-fast read-only parsing) or `nlohmann/json` (for ease of use).

## System Components

1.  **Application Core:** Manages the lifecycle, configuration, and threading model.
2.  **Data Acquisition Layer (Networking):** Handles REST API requests for historical data (7-10 days) and WebSocket streams for real-time tick/trade data.
3.  **Data Processing Layer:** Aggregates raw trades into timeframe-based candles and computes footprint profiles (buy/sell volume per price level).
4.  **Charting & Rendering Engine:** Hardware-accelerated rendering module responsible for drawing the Candlestick, Volume, and complex Footprint charts at 60+ FPS.
5.  **Platform Abstraction Layer:** Handles OS-specific interactions, touching inputs for mobile, and mouse/keyboard for desktop.

## Threading Model
To achieve low latency, the application will use a multi-threaded architecture:
*   **Network Thread:** Exclusively handles socket polling, reading, and parsing raw JSON data.
*   **Processing Thread:** Takes parsed data via lock-free queues (e.g., `moodycamel::ConcurrentQueue`), builds the order book, aggregates candles, and computes footprint data.
*   **Render/UI Thread:** Reads the processed, ready-to-render data structures and draws them to the screen. Never blocked by network I/O.
