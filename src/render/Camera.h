#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>

namespace glora {
namespace render {

// Camera for chart navigation (pan and zoom)
class Camera {
public:
  Camera();

  // Set the visible time range
  void setTimeRange(uint64_t startTime, uint64_t endTime);

  // Set the visible price range
  void setPriceRange(double minPrice, double maxPrice);

  // Get visible ranges
  std::pair<uint64_t, uint64_t> getTimeRange() const;
  std::pair<double, double> getPriceRange() const;

  // Pan the chart
  void pan(double deltaX, double deltaY);

  // Zoom the chart (zoomPoint is in screen coordinates 0-1)
  void zoom(double factor, double zoomPointX, double zoomPointY);

  // Convert screen coordinate to chart coordinate
  std::pair<uint64_t, double> screenToChart(double screenX, double screenY,
                                             int width, int height) const;

  // Convert chart coordinate to screen coordinate
  std::pair<double, double> chartToScreen(uint64_t time, double price,
                                          int width, int height) const;

  // Get zoom level (candles per screen width)
  double getZoomLevel() const { return candlesPerScreen_; }

  // Set chart dimensions
  void setChartArea(double x, double y, double width, double height);

  // Get chart area
  std::pair<double, double> getChartOrigin() const { return {chartX_, chartY_}; }
  std::pair<double, double> getChartSize() const {
    return {chartWidth_, chartHeight_};
  }

  // Reset to auto-fit all data
  void fitToData(uint64_t minTime, uint64_t maxTime, double minPrice,
                 double maxPrice);

private:
  uint64_t startTime_;
  uint64_t endTime_;
  double minPrice_;
  double maxPrice_;

  double chartX_;
  double chartY_;
  double chartWidth_;
  double chartHeight_;

  double candlesPerScreen_;
};

inline Camera::Camera()
    : startTime_(0), endTime_(0), minPrice_(0), maxPrice_(0), chartX_(0),
      chartY_(0), chartWidth_(800), chartHeight_(600), candlesPerScreen_(100) {
}

inline void Camera::setTimeRange(uint64_t startTime, uint64_t endTime) {
  startTime_ = startTime;
  endTime_ = endTime;
  if (endTime > startTime) {
    candlesPerScreen_ = static_cast<double>(endTime - startTime) / 60000; // Approx
  }
}

inline void Camera::setPriceRange(double minPrice, double maxPrice) {
  minPrice_ = minPrice;
  maxPrice_ = maxPrice;
}

inline std::pair<uint64_t, uint64_t> Camera::getTimeRange() const {
  return {startTime_, endTime_};
}

inline std::pair<double, double> Camera::getPriceRange() const {
  return {minPrice_, maxPrice_};
}

inline void Camera::pan(double deltaX, double deltaY) {
  if (chartWidth_ <= 0 || chartHeight_ <= 0)
    return;

  // DeltaX is in normalized screen coordinates (-1 to 1)
  // Convert to time delta
  int64_t timeRange = static_cast<int64_t>(endTime_) - startTime_;
  int64_t timeDelta = static_cast<int64_t>(deltaX * timeRange);

  startTime_ -= timeDelta;
  endTime_ -= timeDelta;

  // DeltaY is in normalized screen coordinates (-1 to 1)
  double priceRange = maxPrice_ - minPrice_;
  double priceDelta = -deltaY * priceRange;

  minPrice_ += priceDelta;
  maxPrice_ += priceDelta;
}

inline void Camera::zoom(double factor, double zoomPointX, double zoomPointY) {
  if (chartWidth_ <= 0 || chartHeight_ <= 0)
    return;

  // Convert zoom point from screen to chart coordinates
  auto [zoomTime, zoomPrice] = screenToChart(zoomPointX, zoomPointY, 1, 1);

  // Apply zoom to time range
  int64_t timeRange = static_cast<int64_t>(endTime_) - startTime_;
  int64_t newTimeRange = static_cast<int64_t>(timeRange * factor);

  if (newTimeRange < 60000) // Minimum 1 minute
    return;
  if (newTimeRange > 365 * 24 * 3600 * 1000LL) // Maximum 1 year
    return;

  // Keep the zoom point fixed in chart coordinates
  double zoomRatio =
      static_cast<double>(zoomTime - startTime_) / static_cast<double>(timeRange);
  startTime_ = zoomTime - static_cast<uint64_t>(newTimeRange * zoomRatio);
  endTime_ = startTime_ + newTimeRange;

  // Apply zoom to price range
  double priceRange = maxPrice_ - minPrice_;
  double newPriceRange = priceRange * factor;

  if (newPriceRange < 0.01) // Minimum price range
    return;

  double priceRatio = (zoomPrice - minPrice_) / priceRange;
  minPrice_ = zoomPrice - newPriceRange * priceRatio;
  maxPrice_ = minPrice_ + newPriceRange;

  candlesPerScreen_ *= factor;
}

inline std::pair<uint64_t, double>
Camera::screenToChart(double screenX, double screenY, int width,
                      int height) const {
  // Convert screen coords to chart area coords
  double chartScreenX = (screenX - chartX_) / chartWidth_;
  double chartScreenY = (screenY - chartY_) / chartHeight_;

  // Flip Y so 0 is at bottom
  chartScreenY = 1.0 - chartScreenY;

  // Clamp to chart area
  chartScreenX = std::max(0.0, std::min(1.0, chartScreenX));
  chartScreenY = std::max(0.0, std::min(1.0, chartScreenY));

  uint64_t time = startTime_ + static_cast<uint64_t>(chartScreenX * (endTime_ - startTime_));
  double price = minPrice_ + chartScreenY * (maxPrice_ - minPrice_);

  return {time, price};
}

inline std::pair<double, double>
Camera::chartToScreen(uint64_t time, double price, int width, int height) const {
  double timeRatio = static_cast<double>(time - startTime_) / static_cast<double>(endTime_ - startTime_);
  double priceRatio = (price - minPrice_) / (maxPrice_ - minPrice_);

  double screenX = chartX_ + timeRatio * chartWidth_;
  double screenY = chartY_ + (1.0 - priceRatio) * chartHeight_;

  return {screenX, screenY};
}

inline void Camera::setChartArea(double x, double y, double width,
                                  double height) {
  chartX_ = x;
  chartY_ = y;
  chartWidth_ = width;
  chartHeight_ = height;
}

inline void Camera::fitToData(uint64_t minTime, uint64_t maxTime,
                               double minPrice, double maxPrice) {
  startTime_ = minTime;
  endTime_ = maxTime;
  minPrice_ = minPrice;
  maxPrice_ = maxPrice;

  // Add 5% padding to price range
  double range = maxPrice - minPrice;
  minPrice_ -= range * 0.05;
  maxPrice_ += range * 0.05;

  // Calculate approximate candles per screen
  if (maxTime > minTime) {
    candlesPerScreen_ = static_cast<double>(maxTime - minTime) / 60000;
  }
}

} // namespace render
} // namespace glora
