#include "ChartRenderer.h"
#include <GL/gl.h>
#include <GL/gl.h>
#include <imgui.h>
#include <iostream>
#include <algorithm>

namespace glora {
namespace render {

ChartRenderer::ChartRenderer() : candleShader_(0), volumeShader_(0) {}

ChartRenderer::~ChartRenderer() {
}

bool ChartRenderer::initialize() {
  initialized_ = true;
  return true;
}

void ChartRenderer::setData(std::shared_ptr<core::ChartDataManager> dataManager) {
  dataManager_ = dataManager;
}

void ChartRenderer::render(int width, int height, const Camera &camera) {
  if (!initialized_ || !dataManager_)
    return;

  auto candles = dataManager_->getCandles();
  if (candles.empty() && dataManager_->getCurrentCandle().volume == 0)
    return;

  // Get chart area from camera
  auto [chartX, chartY] = camera.getChartOrigin();
  auto [chartW, chartH] = camera.getChartSize();

  // Calculate volume chart height
  float volumeHeight = chartH * volumeHeightRatio_;
  float chartAreaHeight = chartH - volumeHeight;

  // Draw background
  ImDrawList *drawList = ImGui::GetWindowDrawList();
  ImVec2 chartMin(chartX, chartY);
  ImVec2 chartMax(chartX + chartW, chartY + chartH);

  // Chart background
  drawList->AddRectFilled(chartMin, chartMax,
                          IM_COL32(10, 12, 18, 255), 0.0f);

  // Draw grid lines
  auto [minPrice, maxPrice] = camera.getPriceRange();
  double priceRange = maxPrice - minPrice;

  // Horizontal grid lines (price levels)
  int numPriceLines = 8;
  for (int i = 0; i <= numPriceLines; i++) {
    float y = chartY + (chartAreaHeight * i / numPriceLines);
    double price = maxPrice - (priceRange * i / numPriceLines);
    drawList->AddLine(ImVec2(chartX, y), ImVec2(chartX + chartW, y),
                      IM_COL32(40, 40, 50, 255), 1.0f);

    // Price label
    char priceStr[32];
    snprintf(priceStr, sizeof(priceStr), "%.2f", price);
    drawList->AddText(ImVec2(chartX + 5, y - 8), IM_COL32(150, 150, 150, 255),
                      priceStr);
  }

  // Draw chart based on type
  switch (chartType_) {
  case ChartType::CANDLESTICK:
    renderCandlesticks(width, height, camera);
    break;
  case ChartType::VOLUME:
    renderVolume(width, height, camera);
    break;
  case ChartType::FOOTPRINT:
    renderFootprint(width, height, camera);
    break;
  }

  // Draw volume chart at bottom
  renderVolume(width, height, camera);
}

void ChartRenderer::renderCandlesticks(int width, int height,
                                         const Camera &camera) {
  if (!dataManager_)
    return;

  ImDrawList *drawList = ImGui::GetWindowDrawList();

  auto [chartX, chartY] = camera.getChartOrigin();
  auto [chartW, chartH] = camera.getChartSize();
  float volumeHeight = chartH * volumeHeightRatio_;
  float chartAreaHeight = chartH - volumeHeight;

  auto candles = dataManager_->getCandles();
  const auto &currentCandle = dataManager_->getCurrentCandle();

  auto [minTime, maxTime] = camera.getTimeRange();
  auto [minPrice, maxPrice] = camera.getPriceRange();

  double timeRange = static_cast<double>(maxTime - minTime);
  double priceRange = maxPrice - minPrice;

  if (timeRange <= 0 || priceRange <= 0)
    return;

  // Calculate candle width based on zoom level
  double candleWidth = chartW / (timeRange / 60000.0) * 0.8;
  candleWidth = std::clamp(candleWidth, 1.0, 50.0);
  double candleSpacing = candleWidth * 0.2;

  // Render each candle
  for (const auto &candle : candles) {
    if (candle.end_time_ms < minTime || candle.start_time_ms > maxTime)
      continue;

    drawCandleImGui(chartX, static_cast<float>(candleWidth), candle,
                    minPrice, priceRange, chartAreaHeight);
    chartX += candleWidth + candleSpacing;
  }

  // Render current candle if visible
  if (currentCandle.volume > 0 && currentCandle.start_time_ms <= maxTime) {
    drawCandleImGui(chartX, static_cast<float>(candleWidth), currentCandle,
                    minPrice, priceRange, chartAreaHeight);
  }
}

void ChartRenderer::renderVolume(int width, int height, const Camera &camera) {
  if (!dataManager_)
    return;

  ImDrawList *drawList = ImGui::GetWindowDrawList();

  auto [chartX, chartY] = camera.getChartOrigin();
  auto [chartW, chartH] = camera.getChartSize();
  float volumeHeight = chartH * volumeHeightRatio_;
  float volumeY = chartY + chartH - volumeHeight;

  auto candles = dataManager_->getCandles();
  const auto &currentCandle = dataManager_->getCurrentCandle();

  auto [minTime, maxTime] = camera.getTimeRange();

  // Find max volume for scaling
  double maxVolume = 0;
  for (const auto &candle : candles) {
    if (candle.volume > maxVolume)
      maxVolume = candle.volume;
  }
  if (currentCandle.volume > maxVolume)
    maxVolume = currentCandle.volume;

  if (maxVolume <= 0)
    return;

  auto [minPrice, maxPrice] = camera.getPriceRange();
  double timeRange = static_cast<double>(maxTime - minTime);

  if (timeRange <= 0)
    return;

  // Calculate bar width
  double barWidth = chartW / (timeRange / 60000.0) * 0.8;
  barWidth = std::clamp(barWidth, 1.0, 50.0);
  double barSpacing = barWidth * 0.2;

  // Render volume bars
  float x = chartX;
  for (const auto &candle : candles) {
    if (candle.end_time_ms < minTime || candle.start_time_ms > maxTime)
      continue;

    bool isBullish = candle.close >= candle.open;
    float barHeight = static_cast<float>((candle.volume / maxVolume) * volumeHeight);

    ImU32 color = isBullish ? IM_COL32(0, 200, 80, 150) : IM_COL32(200, 50, 50, 150);

    drawList->AddRectFilled(
        ImVec2(x, volumeY + volumeHeight - barHeight),
        ImVec2(x + static_cast<float>(barWidth), volumeY + volumeHeight),
        color, 0.0f);

    x += static_cast<float>(barWidth + barSpacing);
  }

  // Current candle volume
  if (currentCandle.volume > 0) {
    bool isBullish = currentCandle.close >= currentCandle.open;
    float barHeight = static_cast<float>((currentCandle.volume / maxVolume) * volumeHeight);

    ImU32 color = isBullish ? IM_COL32(0, 200, 80, 200) : IM_COL32(200, 50, 50, 200);

    drawList->AddRectFilled(
        ImVec2(x, volumeY + volumeHeight - barHeight),
        ImVec2(x + static_cast<float>(barWidth), volumeY + volumeHeight),
        color, 0.0f);
  }
}

void ChartRenderer::renderFootprint(int width, int height,
                                     const Camera &camera) {
  if (!dataManager_)
    return;

  // Footprint is rendered as part of candlestick - show bid/ask volume
  // at each price level inside the candle
  ImDrawList *drawList = ImGui::GetWindowDrawList();

  auto [chartX, chartY] = camera.getChartOrigin();
  auto [chartW, chartH] = camera.getChartSize();
  float volumeHeight = chartH * volumeHeightRatio_;
  float chartAreaHeight = chartH - volumeHeight;

  auto candles = dataManager_->getCandles();
  const auto &currentCandle = dataManager_->getCurrentCandle();

  auto [minPrice, maxPrice] = camera.getPriceRange();
  auto [minTime, maxTime] = camera.getTimeRange();

  double timeRange = static_cast<double>(maxTime - minTime);
  double priceRange = maxPrice - minPrice;

  if (timeRange <= 0 || priceRange <= 0)
    return;

  // Calculate candle width
  double candleWidth = chartW / (timeRange / 60000.0) * 0.8;
  candleWidth = std::clamp(candleWidth, 5.0, 100.0);
  double candleSpacing = candleWidth * 0.2;

  // Render footprint for each candle
  float x = chartX;
  for (const auto &candle : candles) {
    if (candle.end_time_ms < minTime || candle.start_time_ms > maxTime)
      continue;

    drawFootprintImGui(x, static_cast<float>(candleWidth), candle,
                       minPrice, priceRange, chartAreaHeight);
    x += static_cast<float>(candleWidth + candleSpacing);
  }

  // Current candle footprint
  if (currentCandle.volume > 0) {
    drawFootprintImGui(x, static_cast<float>(candleWidth), currentCandle,
                       minPrice, priceRange, chartAreaHeight);
  }
}

void ChartRenderer::drawCandleImGui(float x, float candleWidth,
                                      const core::Candle &candle,
                                      double minPrice, double priceRange,
                                      float chartHeight) {
  ImDrawList *drawList = ImGui::GetWindowDrawList();

  bool isBullish = candle.close >= candle.open;

  // Calculate Y positions
  float highY = chartHeight * (1.0 - (candle.high - minPrice) / priceRange);
  float lowY = chartHeight * (1.0 - (candle.low - minPrice) / priceRange);
  float openY = chartHeight * (1.0 - (candle.open - minPrice) / priceRange);
  float closeY = chartHeight * (1.0 - (candle.close - minPrice) / priceRange);

  // Draw wick
  ImU32 wickColor = isBullish ? IM_COL32(0, 200, 80, 255) : IM_COL32(200, 50, 50, 255);
  drawList->AddLine(ImVec2(x + candleWidth / 2, highY),
                    ImVec2(x + candleWidth / 2, lowY), wickColor, 1.0f);

  // Draw body
  float bodyTop = std::min(openY, closeY);
  float bodyHeight = std::abs(closeY - openY);
  if (bodyHeight < 1.0f)
    bodyHeight = 1.0f; // Minimum 1 pixel

  ImU32 bodyColor = isBullish ? IM_COL32(0, 200, 80, 255) : IM_COL32(200, 50, 50, 255);
  drawList->AddRectFilled(ImVec2(x, bodyTop),
                          ImVec2(x + candleWidth, bodyTop + bodyHeight),
                          bodyColor, 0.0f);
}

void ChartRenderer::drawVolumeBarImGui(float x, float barWidth,
                                         const core::Candle &candle,
                                         double maxVolume,
                                         float volumeHeight) {
  // Volume is rendered in renderVolume
}

void ChartRenderer::drawFootprintImGui(float x, float width,
                                         const core::Candle &candle,
                                         double minPrice, double priceRange,
                                         float chartHeight) {
  ImDrawList *drawList = ImGui::GetWindowDrawList();

  bool isBullish = candle.close >= candle.open;

  // Draw the candle body first
  drawCandleImGui(x, width, candle, minPrice, priceRange, chartHeight);

  // Draw footprint data inside the candle
  float centerX = x + width / 2;

  // Find max volume at any price level for heatmap
  double maxLevelVolume = 0;
  for (const auto &[price, node] : candle.footprint_profile) {
    double totalVol = node.bid_volume + node.ask_volume;
    if (totalVol > maxLevelVolume)
      maxLevelVolume = totalVol;
  }

  if (maxLevelVolume <= 0)
    return;

  // Draw bid/ask volumes
  for (const auto &[price, node] : candle.footprint_profile) {
    float y = chartHeight * (1.0 - (price - minPrice) / priceRange);

    // Only draw if there's enough space
    if (y < 0 || y > chartHeight)
      continue;

    // Calculate intensity based on volume
    float intensity = static_cast<float>((node.bid_volume + node.ask_volume) / maxLevelVolume);

    // Bid volume (left side) - green tint
    if (node.bid_volume > 0) {
      float bidIntensity = static_cast<float>(node.bid_volume / maxLevelVolume);
      ImU32 bidColor = IM_COL32(0, 150, 50, static_cast<int>(bidIntensity * 200 + 55));
      drawList->AddRectFilled(
          ImVec2(x, y - 2),
          ImVec2(centerX - 1, y + 2),
          bidColor, 0.0f);
    }

    // Ask volume (right side) - red tint
    if (node.ask_volume > 0) {
      float askIntensity = static_cast<float>(node.ask_volume / maxLevelVolume);
      ImU32 askColor = IM_COL32(150, 30, 30, static_cast<int>(askIntensity * 200 + 55));
      drawList->AddRectFilled(
          ImVec2(centerX + 1, y - 2),
          ImVec2(x + width, y + 2),
          askColor, 0.0f);
    }
  }
}

} // namespace render
} // namespace glora
