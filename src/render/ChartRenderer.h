#pragma once

#include "Camera.h"
#include "../core/DataModels.h"
#include "../core/ChartDataManager.h"
#include <memory>
#include <vector>

namespace glora {
namespace render {

// Chart types
enum class ChartType { CANDLESTICK, VOLUME, FOOTPRINT };

// Renderer for charts using OpenGL
class ChartRenderer {
public:
  ChartRenderer();
  ~ChartRenderer();

  // Initialize OpenGL resources
  bool initialize();

  // Set the data to render
  void setData(std::shared_ptr<core::ChartDataManager> dataManager);

  // Render the chart
  void render(int width, int height, const Camera &camera);

  // Set chart type
  void setChartType(ChartType type) { chartType_ = type; }

  // Get/Set volume height ratio (0.0 - 0.5)
  void setVolumeHeightRatio(float ratio) { volumeHeightRatio_ = ratio; }
  float getVolumeHeightRatio() const { return volumeHeightRatio_; }

private:
  // Render candlestick chart
  void renderCandlesticks(int width, int height, const Camera &camera);

  // Render volume chart
  void renderVolume(int width, int height, const Camera &camera);

  // Render footprint chart
  void renderFootprint(int width, int height, const Camera &camera);

  // Draw a single candle using ImGui (for simplicity)
  void drawCandleImGui(float x, float candleWidth, const core::Candle &candle,
                        double minPrice, double priceRange, float chartHeight);

  // Draw volume bars using ImGui
  void drawVolumeBarImGui(float x, float barWidth, const core::Candle &candle,
                           double maxVolume, float volumeHeight);

  // Draw footprint using ImGui
  void drawFootprintImGui(float x, float width, const core::Candle &candle,
                           double minPrice, double priceRange, float chartHeight);

  // Shader programs
  unsigned int candleShader_;
  unsigned int volumeShader_;

  // Buffers
  unsigned int candleVAO_, candleVBO_;
  unsigned int volumeVAO_, volumeVBO_;

  std::shared_ptr<core::ChartDataManager> dataManager_;
  ChartType chartType_ = ChartType::CANDLESTICK;
  float volumeHeightRatio_ = 0.2f;

  // Colors
  float bullishColor_[3] = {0.0f, 0.8f, 0.2f};   // Green
  float bearishColor_[3] = {0.8f, 0.1f, 0.1f};    // Red
  float wickColor_[3] = {0.5f, 0.5f, 0.5f};      // Gray
  float gridColor_[3] = {0.2f, 0.2f, 0.2f};      // Dark gray
  float backgroundColor_[3] = {0.05f, 0.05f, 0.08f}; // Dark blue-black

  // ImGui font (use default)
  bool initialized_ = false;
};

} // namespace render
} // namespace glora
