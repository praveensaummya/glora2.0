# Charting & Rendering Engine

## Overview

The charting engine is the most complex UI component. Standard charting libraries rarely support Footprint charts with the necessary performance. Therefore, a custom hardware-accelerated rendering pipeline is required.

## Graphics API

* Use raw **OpenGL (ES 3.0+ for Android compatibility)**, **Vulkan**, or a higher-level abstraction like **Qt RHI (Rendering Hardware Interface)**.
* Using Qt's `QQuickFramebufferObject` or `QQuickGraphicsDevice` allows dropping down to native graphics APIs while staying within the Qt ecosystem for cross-platform UI.

## Chart Types & Rendering Approach

### 1. Candlestick Chart (OHLC)

* **Rendering:** Use instanced rendering. Instead of drawing 4 vertices per candle body and 2 per wick, push a buffer of `(Open, High, Low, Close, Timestamp)` data to the GPU and use a Shader Storage Buffer Object (SSBO) or Instanced Arrays to let the Vertex Shader expand them into rectangles and lines.
* This allows rendering millions of candles with a single OpenGL/Vulkan draw call.

### 2. Volume Chart

* **Rendering:** Instanced rendering of rectangles at the bottom of the screen, colored based on whether the candle was bullish or bearish.

### 3. Footprint Chart (TradingView Style)

* **Complexity:** A Footprint chart shows the volume traded at the bid vs the ask for every price tick inside a candle.
* **Visuals:** Usually represented as text (e.g., "120 x 450") inside the candle body, or visually via color intensity (heatmap).
* **Rendering Text Low-Latency:** Text rendering is slow. Use a **Texture Atlas** (Bitmap font) generated via FreeType. Create a dynamic vertex buffer that maps letters to texture coordinates.
* **Zoom Levels:** Implement Level of Detail (LOD).
  * *Zoomed Out:* Standard candlesticks.
  * *Zoomed In slightly:* Candlesticks with a heatmap overlay representing high-volume price nodes.
  * *Fully Zoomed In:* Exact text numbers (Bid x Ask volume) rendered per price level tick.

## User Interaction

* **Panning & Zooming:** Manage a logical "Camera" mapping UNIX timestamps to the X-axis and Price coordinates to the Y-axis.
* **Mouse/Touch:** Map raw screen coordinates back to the logical Timestamp/Price coordinates to display tooltips and crosshairs instantly.
