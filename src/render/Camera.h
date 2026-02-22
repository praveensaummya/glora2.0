#pragma once

#include <algorithm>
#include <cstdint>
#include <utility>
#include <cmath>

namespace glora {
namespace render {

// Scale types for the price axis
enum class PriceScaleType {
  LINEAR,       // Standard linear scale
  LOGARITHMIC,  // Logarithmic scale (good for assets like BTC)
  PERCENTAGE,   // Percentage scale (relative to initial price)
  INDEXED_100  // Indexed to 100 (like stock charts)
};

// Camera for chart navigation with TradingView-like features
class Camera {
public:
  Camera();

  // === Time Scale (X-Axis) ===
  
  // Set the visible time range
  void setTimeRange(uint64_t startTime, uint64_t endTime);
  
  // Get visible time range
  std::pair<uint64_t, uint64_t> getTimeRange() const;
  
  // Zoom only the time axis (horizontal)
  void zoomTime(double factor, double zoomPointX);
  
  // Pan only the time axis
  void panTime(double deltaX);
  
  // Set right margin (whitespace beyond current candle)
  void setRightMargin(double percent);
  double getRightMargin() const { return rightMarginPercent_; }
  
  // === Price Scale (Y-Axis) ===
  
  // Set the visible price range
  void setPriceRange(double minPrice, double maxPrice);
  
  // Get visible price range
  std::pair<double, double> getPriceRange() const;
  
  // Set price scale type
  void setPriceScaleType(PriceScaleType type) { priceScaleType_ = type; }
  PriceScaleType getPriceScaleType() const { return priceScaleType_; }
  
  // Zoom only the price axis (vertical) - DECOUPLED SCALING
  void zoomPrice(double factor, double zoomPointY);
  
  // Pan only the price axis
  void panPrice(double deltaY);
  
  // Stretch/squish price axis (TradingView-like)
  void stretchPrice(double factor, double pivotY);
  
  // === Combined Operations ===
  
  // Pan both axes
  void pan(double deltaX, double deltaY);
  
  // Zoom both axes (coupled mode)
  void zoom(double factor, double zoomPointX, double zoomPointY);
  
  // === Coordinate Conversion ===
  
  // Convert screen coordinate to chart coordinate
  std::pair<uint64_t, double> screenToChart(double screenX, double screenY,
                                              int width, int height) const;
  
  // Convert chart coordinate to screen coordinate
  std::pair<double, double> chartToScreen(uint64_t time, double price,
                                           int width, int height) const;
  
  // === Chart Area ===
  
  // Set chart dimensions
  void setChartArea(double x, double y, double width, double height);
  
  // Get chart area
  std::pair<double, double> getChartOrigin() const { return {chartX_, chartY_}; }
  std::pair<double, double> getChartSize() const {
    return {chartWidth_, chartHeight_};
  }
  
  // === Data Fitting ===
  
  // Reset to auto-fit all data
  void fitToData(uint64_t minTime, uint64_t maxTime, double minPrice,
                 double maxPrice, double basePrice = 0);
  
  // Fit only price range
  void fitPriceRange(double minPrice, double maxPrice, double basePrice = 0);
  
  // === Scale Conversion ===
  
  // Convert price based on scale type
  double convertPriceToDisplay(double price, double basePrice) const;
  double convertPriceFromDisplay(double displayPrice, double basePrice) const;

private:
  // Time range
  uint64_t startTime_;
  uint64_t endTime_;
  
  // Price range
  double minPrice_;
  double maxPrice_;
  
  // Price scale type
  PriceScaleType priceScaleType_;
  
  // Chart area
  double chartX_;
  double chartY_;
  double chartWidth_;
  double chartHeight_;
  
  // Right margin (whitespace)
  double rightMarginPercent_;
  
  // Helper: apply scale transformation to price
  double applyPriceScale(double price, double basePrice) const;
  double inversePriceScale(double scaledPrice, double basePrice) const;
};

inline Camera::Camera()
    : startTime_(0), endTime_(0), minPrice_(0), maxPrice_(0),
      priceScaleType_(PriceScaleType::LINEAR),
      chartX_(0), chartY_(0), chartWidth_(800), chartHeight_(600),
      rightMarginPercent_(0.1) { // 10% right margin by default
}

inline void Camera::setTimeRange(uint64_t startTime, uint64_t endTime) {
  startTime_ = startTime;
  endTime_ = endTime;
}

inline std::pair<uint64_t, uint64_t> Camera::getTimeRange() const {
  return {startTime_, endTime_};
}

inline void Camera::zoomTime(double factor, double zoomPointX) {
  if (chartWidth_ <= 0) return;
  
  // Convert zoom point from screen to chart coordinates
  double chartX = (zoomPointX - chartX_) / chartWidth_;
  chartX = std::max(0.0, std::min(1.0, chartX));
  
  // Apply zoom to time range
  int64_t timeRange = static_cast<int64_t>(endTime_) - startTime_;
  int64_t newTimeRange = static_cast<int64_t>(timeRange * factor);
  
  if (newTimeRange < 60000) // Minimum 1 minute
    return;
  if (newTimeRange > 365 * 24 * 3600 * 1000LL) // Maximum 1 year
    return;
  
  // Keep the zoom point fixed
  uint64_t zoomTime = startTime_ + static_cast<uint64_t>(chartX * timeRange);
  startTime_ = zoomTime - static_cast<uint64_t>(newTimeRange * chartX);
  endTime_ = startTime_ + newTimeRange;
}

inline void Camera::panTime(double deltaX) {
  if (chartWidth_ <= 0) return;
  
  int64_t timeRange = static_cast<int64_t>(endTime_) - startTime_;
  int64_t timeDelta = static_cast<int64_t>(deltaX * timeRange);
  
  startTime_ -= timeDelta;
  endTime_ -= timeDelta;
  
  // Don't pan into negative time
  if (startTime_ < 0) {
    int64_t diff = -static_cast<int64_t>(startTime_);
    startTime_ = 0;
    endTime_ += diff;
  }
}

inline void Camera::setRightMargin(double percent) {
  rightMarginPercent_ = std::max(0.0, std::min(1.0, percent)); // 0-100%
}

inline void Camera::setPriceRange(double minPrice, double maxPrice) {
  minPrice_ = minPrice;
  maxPrice_ = maxPrice;
}

inline std::pair<double, double> Camera::getPriceRange() const {
  return {minPrice_, maxPrice_};
}

inline double Camera::applyPriceScale(double price, double basePrice) const {
  switch (priceScaleType_) {
    case PriceScaleType::LINEAR:
      return price;
    case PriceScaleType::LOGARITHMIC:
      return std::log10(price);
    case PriceScaleType::PERCENTAGE:
      return basePrice > 0 ? ((price - basePrice) / basePrice) * 100.0 : 0;
    case PriceScaleType::INDEXED_100:
      return basePrice > 0 ? (price / basePrice) * 100.0 : 100.0;
    default:
      return price;
  }
}

inline double Camera::inversePriceScale(double scaledPrice, double basePrice) const {
  switch (priceScaleType_) {
    case PriceScaleType::LINEAR:
      return scaledPrice;
    case PriceScaleType::LOGARITHMIC:
      return std::pow(10.0, scaledPrice);
    case PriceScaleType::PERCENTAGE:
      return basePrice > 0 ? basePrice * (1.0 + scaledPrice / 100.0) : 0;
    case PriceScaleType::INDEXED_100:
      return basePrice > 0 ? basePrice * (scaledPrice / 100.0) : 0;
    default:
      return scaledPrice;
  }
}

inline double Camera::convertPriceToDisplay(double price, double basePrice) const {
  return applyPriceScale(price, basePrice);
}

inline double Camera::convertPriceFromDisplay(double displayPrice, double basePrice) const {
  return inversePriceScale(displayPrice, basePrice);
}

inline void Camera::zoomPrice(double factor, double zoomPointY) {
  if (chartHeight_ <= 0) return;
  
  // Convert zoom point from screen to chart coordinates
  double chartY = (zoomPointY - chartY_) / chartHeight_;
  chartY = 1.0 - std::max(0.0, std::min(1.0, chartY)); // Flip Y
  
  // Apply zoom to price range
  double priceRange = maxPrice_ - minPrice_;
  double newPriceRange = priceRange * factor;
  
  if (newPriceRange < 0.01) // Minimum price range
    return;
  
  // Keep the zoom point fixed
  double zoomPrice = minPrice_ + priceRange * chartY;
  minPrice_ = zoomPrice - newPriceRange * chartY;
  maxPrice_ = minPrice_ + newPriceRange;
}

inline void Camera::panPrice(double deltaY) {
  if (chartHeight_ <= 0) return;
  
  double priceRange = maxPrice_ - minPrice_;
  double priceDelta = -deltaY * priceRange;
  
  minPrice_ += priceDelta;
  maxPrice_ += priceDelta;
}

inline void Camera::stretchPrice(double factor, double pivotY) {
  if (chartHeight_ <= 0) return;
  
  // Convert pivot from screen to chart coordinates  
  double chartY = (pivotY - chartY_) / chartHeight_;
  chartY = 1.0 - std::max(0.0, std::min(1.0, chartY));
  
  // Stretch/squish around pivot point
  double priceRange = maxPrice_ - minPrice_;
  double newPriceRange = priceRange * factor;
  
  if (newPriceRange < 0.01 || newPriceRange > 1e10)
    return;
  
  double pivotPrice = minPrice_ + priceRange * chartY;
  minPrice_ = pivotPrice - newPriceRange * chartY;
  maxPrice_ = minPrice_ + newPriceRange;
}

inline void Camera::pan(double deltaX, double deltaY) {
  panTime(deltaX);
  panPrice(deltaY);
}

inline void Camera::zoom(double factor, double zoomPointX, double zoomPointY) {
  zoomTime(factor, zoomPointX);
  zoomPrice(factor, zoomPointY);
}

inline std::pair<uint64_t, double>
Camera::screenToChart(double screenX, double screenY,
                      int width, int height) const {
  // Convert screen coords to chart area coords
  double chartScreenX = (screenX - chartX_) / chartWidth_;
  double chartScreenY = (screenY - chartY_) / chartHeight_;
  
  // Flip Y so 0 is at bottom
  chartScreenY = 1.0 - chartScreenY;
  
  // Clamp to chart area
  chartScreenX = std::max(0.0, std::min(1.0, chartScreenX));
  chartScreenY = std::max(0.0, std::min(1.0, chartScreenY));
  
  // Account for right margin
  double effectiveWidth = 1.0 - rightMarginPercent_;
  
  uint64_t time = startTime_ + static_cast<uint64_t>((chartScreenX / effectiveWidth) * (endTime_ - startTime_));
  double price = minPrice_ + chartScreenY * (maxPrice_ - minPrice_);
  
  return {time, price};
}

inline std::pair<double, double>
Camera::chartToScreen(uint64_t time, double price, int width, int height) const {
  // Account for right margin
  double effectiveWidth = 1.0 - rightMarginPercent_;
  
  double timeRatio = static_cast<double>(time - startTime_) / static_cast<double>(endTime_ - startTime_);
  timeRatio = std::max(0.0, std::min(1.0, timeRatio)); // Clamp
  
  double priceRatio = (price - minPrice_) / (maxPrice_ - minPrice_);
  priceRatio = std::max(0.0, std::min(1.0, priceRatio)); // Clamp
  
  double screenX = chartX_ + (timeRatio * effectiveWidth) * chartWidth_;
  double screenY = chartY_ + (1.0 - priceRatio) * chartHeight_;
  
  return {screenX, screenY};
}

inline void Camera::setChartArea(double x, double y, double width, double height) {
  chartX_ = x;
  chartY_ = y;
  chartWidth_ = width;
  chartHeight_ = height;
}

inline void Camera::fitToData(uint64_t minTime, uint64_t maxTime,
                               double minPrice, double maxPrice, double basePrice) {
  startTime_ = minTime;
  endTime_ = maxTime;
  
  // Apply price scale transformation for fitting
  double scaledMin = applyPriceScale(minPrice, basePrice);
  double scaledMax = applyPriceScale(maxPrice, basePrice);
  
  // Add 5% padding
  double range = scaledMax - scaledMin;
  scaledMin -= range * 0.05;
  scaledMax += range * 0.05;
  
  // Convert back
  minPrice_ = inversePriceScale(scaledMin, basePrice);
  maxPrice_ = inversePriceScale(scaledMax, basePrice);
}

inline void Camera::fitPriceRange(double minPrice, double maxPrice, double basePrice) {
  // Apply price scale transformation for fitting
  double scaledMin = applyPriceScale(minPrice, basePrice);
  double scaledMax = applyPriceScale(maxPrice, basePrice);
  
  // Add 5% padding
  double range = scaledMax - scaledMin;
  scaledMin -= range * 0.05;
  scaledMax += range * 0.05;
  
  // Convert back
  minPrice_ = inversePriceScale(scaledMin, basePrice);
  maxPrice_ = inversePriceScale(scaledMax, basePrice);
}

} // namespace render
} // namespace glora
