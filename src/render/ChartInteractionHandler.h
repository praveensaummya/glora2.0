#pragma once

#include "Camera.h"
#include "ChartLayer.h"
#include "ChartData.h"
#include <functional>
#include <vector>
#include <optional>

namespace glora {
namespace render {

// Snap mode for magnet functionality
enum class SnapMode {
  NONE,       // No snapping
  PRICE,      // Snap to nearest price level
  TIME,       // Snap to nearest time
  CANDLE,     // Snap to OHLC values
  ALL         // Snap to all (price + time + OHLC)
};

// Crosshair data for sync between charts
struct CrosshairData {
  double screenX;
  double screenY;
  uint64_t time;
  double price;
  bool visible = false;
};

// Interaction handler for smart chart interactivity
class ChartInteractionHandler {
public:
  ChartInteractionHandler();
  ~ChartInteractionHandler() = default;
  
  // === Magnet Mode ===
  
  void setSnapMode(SnapMode mode) { snapMode_ = mode; }
  SnapMode getSnapMode() const { return snapMode_; }
  
  void setSnapTolerance(double tolerance) { snapTolerance_ = tolerance; }
  double getSnapTolerance() const { return snapTolerance_; }
  
  // Snap a position to nearest snap point (magnet effect)
  std::pair<double, double> applyMagnet(double screenX, double screenY,
                                         const Camera& camera,
                                         const class ChartData& data) const;
  
  // === Crosshair ===
  
  const CrosshairData& getCrosshair() const { return crosshair_; }
  
  void setCrosshairPosition(double screenX, double screenY,
                            const Camera& camera,
                            const class ChartData& data);
  
  void hideCrosshair() { crosshair_.visible = false; }
  void showCrosshair() { crosshair_.visible = true; }
  
  // === Multi-Chart Sync ===
  
  // Register this chart for crosshair sync
  void registerForSync(const std::string& chartId);
  
  // Update crosshair and notify other synced charts
  void updateCrosshairSync(double screenX, double screenY,
                           const Camera& camera,
                           const class ChartData& data);
  
  // Callback for when crosshair position changes (for sync)
  using CrosshairCallback = std::function<void(const std::string& chartId, 
                                                const CrosshairData& data)>;
  void setOnCrosshairChange(CrosshairCallback callback) { onCrosshairChange_ = callback; }
  
  // === Drawing Tools ===
  
  // Current drawing mode
  enum class DrawMode {
    NONE,
    TRENDLINE,
    HORIZONTAL_LINE,
    RECTANGLE,
    FIBONACCI,
    TEXT
  };
  
  void setDrawMode(DrawMode mode) { drawMode_ = mode; }
  DrawMode getDrawMode() const { return drawMode_; }
  
  // Start/end drawing points
  std::pair<double, double> getDrawStart() const { return drawStart_; }
  std::pair<double, double> getDrawEnd() const { return drawEnd_; }
  bool isDrawing() const { return isDrawing_; }
  
  void startDrawing(double screenX, double screenY, const Camera& camera);
  void updateDrawing(double screenX, double screenY, const Camera& camera,
                     const class ChartData& data);
  void endDrawing(class ObjectTree& objectTree);
  
  // === Pane Docking ===
  
  // Check if cursor is in "dock zone" for overlaying indicators
  bool isInDockZone(double screenY, int totalHeight) const;
  std::optional<double> getDockPosition(double screenY, int totalHeight) const;
  
  // === Mouse State ===
  
  enum class MouseButton {
    NONE,
    LEFT,
    MIDDLE,
    RIGHT
  };
  
  void setMouseButton(MouseButton button) { mouseButton_ = button; }
  MouseButton getMouseButton() const { return mouseButton_; }
  
  void setMousePosition(double x, double y) { mouseX_ = x; mouseY_ = y; }
  double getMouseX() const { return mouseX_; }
  double getMouseY() const { return mouseY_; }

private:
  // Magnet settings
  SnapMode snapMode_ = SnapMode::ALL;
  double snapTolerance_ = 10.0; // pixels
  
  // Crosshair
  CrosshairData crosshair_;
  
  // Multi-chart sync
  std::string chartId_;
  std::vector<std::string> syncedChartIds_;
  CrosshairCallback onCrosshairChange_;
  
  // Drawing
  DrawMode drawMode_ = DrawMode::NONE;
  std::pair<double, double> drawStart_;
  std::pair<double, double> drawEnd_;
  bool isDrawing_ = false;
  
  // Mouse state
  MouseButton mouseButton_ = MouseButton::NONE;
  double mouseX_ = 0;
  double mouseY_ = 0;
  
  // Helper: find nearest price in data
  double findNearestPrice(double screenY, const Camera& camera, 
                          const class ChartData& data) const {
    if (data.getAllCandles().empty()) return 0;
    
    // Convert screen Y to chart price
    auto [time, price] = camera.screenToChart(0, screenY, 1, 1);
    
    // Use ChartData's findNearestPriceLevel
    return data.findNearestPriceLevel(price, 0.01);
  }
  
  // Helper: find nearest time in data  
  uint64_t findNearestTime(double screenX, const Camera& camera,
                           const class ChartData& data) const {
    if (data.getAllCandles().empty()) return 0;
    
    // Convert screen X to chart time
    auto [time, price] = camera.screenToChart(screenX, 0, 1, 1);
    
    // Use ChartData's findNearestTime
    return data.findNearestTime(time);
  }
  
  // Helper: find nearest OHLC value
  std::optional<double> findNearestOHLC(double screenX, double screenY,
                                         const Camera& camera,
                                         const class ChartData& data) const {
    if (data.getAllCandles().empty()) return std::nullopt;
    
    // Convert screen coordinates to chart coordinates
    auto [time, price] = camera.screenToChart(screenX, screenY, 1, 1);
    
    // Use ChartData's findNearestOHLC
    return data.findNearestOHLC(time, price);
  }
};

inline ChartInteractionHandler::ChartInteractionHandler() {}

// Snap to nearest point (magnet effect)
inline std::pair<double, double> 
ChartInteractionHandler::applyMagnet(double screenX, double screenY,
                                     const Camera& camera,
                                     const class ChartData& data) const {
  double snappedX = screenX;
  double snappedY = screenY;
  
  if (snapMode_ == SnapMode::NONE) {
    return {snappedX, snappedY};
  }
  
  // Snap to price level
  if (snapMode_ == SnapMode::PRICE || snapMode_ == SnapMode::ALL) {
    double nearestPrice = findNearestPrice(screenY, camera, data);
    if (nearestPrice > 0) {
      auto [snapX, snapY] = camera.chartToScreen(0, nearestPrice, 1, 1);
      if (std::abs(snapY - screenY) < snapTolerance_) {
        snappedY = snapY;
      }
    }
  }
  
  // Snap to time
  if (snapMode_ == SnapMode::TIME || snapMode_ == SnapMode::ALL) {
    uint64_t nearestTime = findNearestTime(screenX, camera, data);
    if (nearestTime > 0) {
      auto [snapX, snapY] = camera.chartToScreen(nearestTime, 0, 1, 1);
      if (std::abs(snapX - screenX) < snapTolerance_) {
        snappedX = snapX;
      }
    }
  }
  
  // Snap to OHLC
  if (snapMode_ == SnapMode::CANDLE || snapMode_ == SnapMode::ALL) {
    auto ohlc = findNearestOHLC(screenX, screenY, camera, data);
    if (ohlc.has_value()) {
      auto [snapX, snapY] = camera.chartToScreen(0, ohlc.value(), 1, 1);
      if (std::abs(snapY - screenY) < snapTolerance_) {
        snappedY = snapY;
      }
    }
  }
  
  return {snappedX, snappedY};
}

inline void ChartInteractionHandler::setCrosshairPosition(
    double screenX, double screenY,
    const Camera& camera,
    const class ChartData& data) {
  
  // Apply magnet
  auto [snapX, snapY] = applyMagnet(screenX, screenY, camera, data);
  
  // Convert to chart coordinates
  auto [time, price] = camera.screenToChart(snapX, snapY, 1, 1);
  
  crosshair_.screenX = snapX;
  crosshair_.screenY = snapY;
  crosshair_.time = time;
  crosshair_.price = price;
  crosshair_.visible = true;
}

inline void ChartInteractionHandler::registerForSync(const std::string& chartId) {
  chartId_ = chartId;
}

inline void ChartInteractionHandler::updateCrosshairSync(
    double screenX, double screenY,
    const Camera& camera,
    const class ChartData& data) {
  
  setCrosshairPosition(screenX, screenY, camera, data);
  
  // Notify other synced charts
  if (onCrosshairChange_) {
    onCrosshairChange_(chartId_, crosshair_);
  }
}

inline void ChartInteractionHandler::startDrawing(double screenX, double screenY, 
                                                   const Camera& camera) {
  if (drawMode_ != DrawMode::NONE) {
    drawStart_ = {screenX, screenY};
    drawEnd_ = {screenX, screenY};
    isDrawing_ = true;
  }
}

inline void ChartInteractionHandler::updateDrawing(double screenX, double screenY,
                                                   const Camera& camera,
                                                   const class ChartData& data) {
  if (isDrawing_) {
    // Apply magnet while drawing
    auto [snapX, snapY] = applyMagnet(screenX, screenY, camera, data);
    drawEnd_ = {snapX, snapY};
  }
}

inline void ChartInteractionHandler::endDrawing(ObjectTree& objectTree) {
  isDrawing_ = false;
  // Drawing completion would create the actual object here
  // This is handled by the chart renderer
}

inline bool ChartInteractionHandler::isInDockZone(double screenY, int totalHeight) const {
  // Top or bottom 10% is dock zone
  double zoneSize = totalHeight * 0.1;
  return screenY < zoneSize || screenY > totalHeight - zoneSize;
}

inline std::optional<double> ChartInteractionHandler::getDockPosition(double screenY, int totalHeight) const {
  if (isInDockZone(screenY, totalHeight)) {
    // Snap to edges
    if (screenY < totalHeight * 0.1) {
      return 0.0; // Top dock
    } else {
      return static_cast<double>(totalHeight); // Bottom dock
    }
  }
  return std::nullopt;
}

} // namespace render
} // namespace glora
