#pragma once

#include <string>
#include <vector>
#include <memory>
#include <unordered_map>
#include <functional>

namespace glora {
namespace render {

// Forward declarations
class ChartLayer;

// Base class for all chart objects (drawings, indicators, etc.)
class ChartObject {
public:
  ChartObject(const std::string& name, int zOrder = 0);
  virtual ~ChartObject() = default;
  
  // Object properties
  const std::string& getName() const { return name_; }
  void setName(const std::string& name) { name_ = name; }
  
  int getZOrder() const { return zOrder_; }
  void setZOrder(int zOrder) { zOrder_ = zOrder; }
  
  // Visibility per timeframe (multi-timeframe analysis)
  bool isVisible(int timeframeMinutes) const;
  void setVisibleForTimeframe(int timeframeMinutes, bool visible);
  void setVisibleForAllTimeframes(bool visible);
  
  // Lock/Unlock (prevent accidental edits)
  bool isLocked() const { return locked_; }
  void setLocked(bool locked) { locked_ = locked; }
  
  // Selection state
  bool isSelected() const { return selected_; }
  void setSelected(bool selected) { selected_ = selected; }
  
  // Virtual render method - override in subclasses
  virtual void render(class Camera& camera, int width, int height) = 0;
  
  // Hit test for selection
  virtual bool hitTest(double screenX, double screenY, double tolerance) const { return false; }
  
protected:
  std::string name_;
  int zOrder_;
  bool locked_ = false;
  bool selected_ = false;
  
  // Visibility map: timeframe in minutes -> visible
  std::unordered_map<int, bool> timeframeVisibility_;
};

// Object types
enum class ObjectType {
  TRENDLINE,
  HORIZONTAL_LINE,
  FIBONACCI,
  RECTANGLE,
  TEXT,
  SHAPE,
  INDICATOR,
  DRAWING
};

// Layer containing multiple objects
class ChartLayer {
public:
  ChartLayer(const std::string& name);
  ~ChartLayer();
  
  // Layer properties
  const std::string& getName() const { return name_; }
  void setName(const std::string& name) { name_ = name; }
  
  // Layer visibility
  bool isVisible() const { return visible_; }
  void setVisible(bool visible) { visible_ = visible; }
  
  // Layer lock
  bool isLocked() const { return locked_; }
  void setLocked(bool locked) { locked_ = locked; }
  
  // Object management
  void addObject(std::shared_ptr<ChartObject> obj);
  void removeObject(const std::string& objectName);
  std::shared_ptr<ChartObject> getObject(const std::string& objectName) const;
  
  // Get all objects (sorted by z-order)
  std::vector<std::shared_ptr<ChartObject>> getObjects() const;
  
  // Render all visible objects
  void render(class Camera& camera, int width, int height, int currentTimeframe);
  
  // Clear all objects
  void clear();
  
private:
  std::string name_;
  bool visible_ = true;
  bool locked_ = false;
  std::vector<std::shared_ptr<ChartObject>> objects_;
};

// Object Tree - manages all layers (like TradingView's Object Tree)
class ObjectTree {
public:
  ObjectTree();
  ~ObjectTree();
  
  // Layer management
  void addLayer(const std::string& layerName);
  void removeLayer(const std::string& layerName);
  std::shared_ptr<ChartLayer> getLayer(const std::string& layerName) const;
  std::vector<std::shared_ptr<ChartLayer>> getLayers() const;
  
  // Grouping: create a folder/group
  void createGroup(const std::string& groupName);
  void addToGroup(const std::string& objectName, const std::string& groupName);
  void hideGroup(const std::string& groupName, bool hide);
  
  // Add object to a layer
  void addObject(std::shared_ptr<ChartObject> obj, const std::string& layerName = "Default");
  
  // Render all layers
  void render(class Camera& camera, int width, int height, int currentTimeframe);
  
  // Hit test - find object at position
  std::shared_ptr<ChartObject> hitTest(double screenX, double screenY, double tolerance) const;
  
  // Serialize/Deserialize for saving drawings
  std::string serialize() const;
  void deserialize(const std::string& data);
  
  // Clear all
  void clear();
  
private:
  std::unordered_map<std::string, std::shared_ptr<ChartLayer>> layers_;
  std::vector<std::string> layerOrder_; // Order of layers (bottom to top)
  
  // Groups (folders)
  std::unordered_map<std::string, std::vector<std::string>> groups_;
};

// Implementation

inline ChartObject::ChartObject(const std::string& name, int zOrder)
    : name_(name), zOrder_(zOrder) {
  // Default: visible on all timeframes
  timeframeVisibility_[0] = true; // 0 means "all"
}

inline bool ChartObject::isVisible(int timeframeMinutes) const {
  // Check specific timeframe first
  auto it = timeframeVisibility_.find(timeframeMinutes);
  if (it != timeframeVisibility_.end()) {
    return it->second;
  }
  // Check "all timeframes" setting
  auto itAll = timeframeVisibility_.find(0);
  if (itAll != timeframeVisibility_.end()) {
    return itAll->second;
  }
  return true; // Default visible
}

inline void ChartObject::setVisibleForTimeframe(int timeframeMinutes, bool visible) {
  timeframeVisibility_[timeframeMinutes] = visible;
}

inline void ChartObject::setVisibleForAllTimeframes(bool visible) {
  timeframeVisibility_[0] = visible;
}

inline ChartLayer::ChartLayer(const std::string& name) : name_(name) {}

inline ChartLayer::~ChartLayer() {}

inline void ChartLayer::addObject(std::shared_ptr<ChartObject> obj) {
  objects_.push_back(obj);
  // Sort by z-order
  std::sort(objects_.begin(), objects_.end(),
    [](const std::shared_ptr<ChartObject>& a, const std::shared_ptr<ChartObject>& b) {
      return a->getZOrder() < b->getZOrder();
    });
}

inline void ChartLayer::removeObject(const std::string& objectName) {
  objects_.erase(
    std::remove_if(objects_.begin(), objects_.end(),
      [&objectName](const std::shared_ptr<ChartObject>& obj) {
        return obj->getName() == objectName;
      }),
    objects_.end()
  );
}

inline std::shared_ptr<ChartObject> ChartLayer::getObject(const std::string& objectName) const {
  for (const auto& obj : objects_) {
    if (obj->getName() == objectName) {
      return obj;
    }
  }
  return nullptr;
}

inline std::vector<std::shared_ptr<ChartObject>> ChartLayer::getObjects() const {
  return objects_;
}

inline void ChartLayer::render(Camera& camera, int width, int height, int currentTimeframe) {
  if (!visible_ || locked_) return;
  
  for (const auto& obj : objects_) {
    if (obj->isVisible(currentTimeframe)) {
      obj->render(camera, width, height);
    }
  }
}

inline void ChartLayer::clear() {
  objects_.clear();
}

inline ObjectTree::ObjectTree() {
  // Create default layer
  addLayer("Default");
}

inline ObjectTree::~ObjectTree() {}

inline void ObjectTree::addLayer(const std::string& layerName) {
  if (layers_.find(layerName) == layers_.end()) {
    layers_[layerName] = std::make_shared<ChartLayer>(layerName);
    layerOrder_.push_back(layerName);
  }
}

inline void ObjectTree::removeLayer(const std::string& layerName) {
  if (layerName != "Default") { // Don't remove default layer
    layers_.erase(layerName);
    layerOrder_.erase(
      std::remove(layerOrder_.begin(), layerOrder_.end(), layerName),
      layerOrder_.end()
    );
  }
}

inline std::shared_ptr<ChartLayer> ObjectTree::getLayer(const std::string& layerName) const {
  auto it = layers_.find(layerName);
  if (it != layers_.end()) {
    return it->second;
  }
  return nullptr;
}

inline std::vector<std::shared_ptr<ChartLayer>> ObjectTree::getLayers() const {
  std::vector<std::shared_ptr<ChartLayer>> result;
  for (const auto& name : layerOrder_) {
    auto it = layers_.find(name);
    if (it != layers_.end()) {
      result.push_back(it->second);
    }
  }
  return result;
}

inline void ObjectTree::createGroup(const std::string& groupName) {
  groups_[groupName] = {};
}

inline void ObjectTree::addToGroup(const std::string& objectName, const std::string& groupName) {
  groups_[groupName].push_back(objectName);
}

inline void ObjectTree::hideGroup(const std::string& groupName, bool hide) {
  // Hide all objects in the group
  auto it = groups_.find(groupName);
  if (it != groups_.end()) {
    for (const auto& objName : it->second) {
      for (const auto& layer : layers_) {
        auto obj = layer.second->getObject(objName);
        if (obj) {
          obj->setVisibleForAllTimeframes(!hide);
        }
      }
    }
  }
}

inline void ObjectTree::addObject(std::shared_ptr<ChartObject> obj, const std::string& layerName) {
  auto layer = getLayer(layerName);
  if (layer) {
    layer->addObject(obj);
  } else {
    // Create layer if doesn't exist
    addLayer(layerName);
    layer = getLayer(layerName);
    if (layer) {
      layer->addObject(obj);
    }
  }
}

inline void ObjectTree::render(Camera& camera, int width, int height, int currentTimeframe) {
  // Render layers in order (bottom to top)
  for (const auto& name : layerOrder_) {
    auto it = layers_.find(name);
    if (it != layers_.end()) {
      it->second->render(camera, width, height, currentTimeframe);
    }
  }
}

inline std::shared_ptr<ChartObject> ObjectTree::hitTest(double screenX, double screenY, double tolerance) const {
  // Check from top layer to bottom (reverse order for selection)
  for (auto it = layerOrder_.rbegin(); it != layerOrder_.rend(); ++it) {
    auto layerIt = layers_.find(*it);
    if (layerIt != layers_.end()) {
      const auto& objects = layerIt->second->getObjects();
      for (auto objIt = objects.rbegin(); objIt != objects.rend(); ++objIt) {
        if ((*objIt)->hitTest(screenX, screenY, tolerance)) {
          return *objIt;
        }
      }
    }
  }
  return nullptr;
}

inline void ObjectTree::clear() {
  layers_.clear();
  layerOrder_.clear();
  groups_.clear();
  addLayer("Default"); // Recreate default
}

} // namespace render
} // namespace glora
