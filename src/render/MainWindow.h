#pragma once

#include "../core/DataModels.h"
#include "ChartInteractionHandler.h"
#include <memory>

namespace glora {
namespace render {

class MainWindow {
public:
  MainWindow(int width, int height, const std::string &title);
  ~MainWindow();

  // Initialize display and graphics context
  bool initialize();

  // Start the rendering loop
  void run();

  // Update the chart with new data from the network thread
  void updateSymbolData(const core::SymbolData &data);

  // Add a single raw tick for testing
  void addRawTick(const core::Tick &tick);

private:
  struct Impl;
  std::unique_ptr<Impl> pImpl;
};

} // namespace render
} // namespace glora
