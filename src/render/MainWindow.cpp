#include "MainWindow.h"
#include "ChartRenderer.h"
#include "Camera.h"
#include "../core/ChartDataManager.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>
#include <iostream>
#include <vector>
#include <cmath>

namespace glora {
namespace render {

struct MainWindow::Impl {
  int width;
  int height;
  std::string title;
  SDL_Window *window = nullptr;
  SDL_GLContext gl_context = nullptr;
  bool done = false;

  // Chart components
  std::shared_ptr<core::ChartDataManager> chartDataManager;
  std::shared_ptr<ChartRenderer> chartRenderer;
  std::shared_ptr<Camera> camera;

  // Mouse state
  bool isDragging = false;
  double lastMouseX = 0;
  double lastMouseY = 0;
  double mouseWheelAccum = 0;

  // UI state
  int selectedTimeframe = 1; // 1 = M1, 5 = M5, etc.
  int selectedChartType = 0; // 0 = Candlestick, 1 = Volume, 2 = Footprint
};

MainWindow::MainWindow(int width, int height, const std::string &title)
    : pImpl(std::make_unique<Impl>()) {
  pImpl->width = width;
  pImpl->height = height;
  pImpl->title = title;

  // Initialize chart components
  pImpl->chartDataManager = std::make_shared<core::ChartDataManager>(core::Timeframe::M1);
  pImpl->chartRenderer = std::make_shared<ChartRenderer>();
  pImpl->camera = std::make_shared<Camera>();
}

MainWindow::~MainWindow() {
  if (pImpl->gl_context) {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(pImpl->gl_context);
  }
  if (pImpl->window) {
    SDL_DestroyWindow(pImpl->window);
  }
  SDL_Quit();
}

bool MainWindow::initialize() {
  if (SDL_Init(SDL_INIT_VIDEO) != 0) {
    std::cerr << "Error: " << SDL_GetError() << std::endl;
    return false;
  }

  // Decide GL+GLSL versions
#if defined(IMGUI_IMPL_OPENGL_ES2)
  const char *glsl_version = "#version 100";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#elif defined(__APPLE__)
  const char *glsl_version = "#version 150";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 2);
#else
  const char *glsl_version = "#version 130";
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, 0);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
  SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
#endif

  SDL_WindowFlags window_flags =
      (SDL_WindowFlags)(SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE |
                        SDL_WINDOW_ALLOW_HIGHDPI);
  pImpl->window = SDL_CreateWindow(pImpl->title.c_str(), SDL_WINDOWPOS_CENTERED,
                                   SDL_WINDOWPOS_CENTERED, pImpl->width,
                                   pImpl->height, window_flags);

  if (pImpl->window == nullptr) {
    std::cerr << "Error: SDL_CreateWindow failed: " << SDL_GetError() << std::endl;
    return false;
  }

  pImpl->gl_context = SDL_GL_CreateContext(pImpl->window);
  SDL_GL_MakeCurrent(pImpl->window, pImpl->gl_context);
  SDL_GL_SetSwapInterval(1); // Enable vsync

  // Setup Dear ImGui context
  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  (void)io;
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

  ImGui::StyleColorsDark();

  ImGuiStyle &style = ImGui::GetStyle();
  if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
    style.WindowRounding = 0.0f;
    style.Colors[ImGuiCol_WindowBg].w = 1.0f;
  }

  ImGui_ImplSDL2_InitForOpenGL(pImpl->window, pImpl->gl_context);
  ImGui_ImplOpenGL3_Init(glsl_version);

  // Initialize chart renderer
  pImpl->chartRenderer->initialize();
  pImpl->chartRenderer->setData(pImpl->chartDataManager);

  return true;
}

void MainWindow::run() {
  ImGuiIO &io = ImGui::GetIO();
  pImpl->done = false;

  while (!pImpl->done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);

      if (event.type == SDL_QUIT)
        pImpl->done = true;
      if (event.type == SDL_WINDOWEVENT &&
          event.window.event == SDL_WINDOWEVENT_CLOSE &&
          event.window.windowID == SDL_GetWindowID(pImpl->window))
        pImpl->done = true;

      // Handle mouse wheel for zoom
      if (event.type == SDL_MOUSEWHEEL) {
        pImpl->mouseWheelAccum += event.wheel.y;
        if (pImpl->mouseWheelAccum >= 1.0 || pImpl->mouseWheelAccum <= -1.0) {
          int zoomDir = (pImpl->mouseWheelAccum > 0) ? 1 : -1;
          double zoomFactor = (zoomDir > 0) ? 0.9 : 1.1;

          // Get mouse position in normalized coordinates
          int mouseX, mouseY;
          SDL_GetMouseState(&mouseX, &mouseY);
          double normX = static_cast<double>(mouseX) / io.DisplaySize.x;
          double normY = static_cast<double>(mouseY) / io.DisplaySize.y;

          pImpl->camera->zoom(zoomFactor, normX, normY);
          pImpl->mouseWheelAccum = 0;
        }
      }

      // Handle mouse drag for pan
      if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          pImpl->isDragging = true;
          pImpl->lastMouseX = event.button.x;
          pImpl->lastMouseY = event.button.y;
        }
      }

      if (event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          pImpl->isDragging = false;
        }
      }

      if (event.type == SDL_MOUSEMOTION && pImpl->isDragging) {
        double deltaX = (event.motion.x - pImpl->lastMouseX) / io.DisplaySize.x * 2.0;
        double deltaY = (event.motion.y - pImpl->lastMouseY) / io.DisplaySize.y * 2.0;
        pImpl->camera->pan(-deltaX, deltaY); // Negative X for natural pan direction
        pImpl->lastMouseX = event.motion.x;
        pImpl->lastMouseY = event.motion.y;
      }
    }

    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplSDL2_NewFrame();
    ImGui::NewFrame();

    ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    // ===== CHART WINDOW =====
    ImGui::Begin("Glora Chart - BTCUSDT", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_MenuBar);

    // Menu bar
    if (ImGui::BeginMenuBar()) {
      if (ImGui::BeginMenu("Timeframe")) {
        if (ImGui::MenuItem("1 Minute", "1", pImpl->selectedTimeframe == 1))
          pImpl->selectedTimeframe = 1;
        if (ImGui::MenuItem("5 Minutes", "5", pImpl->selectedTimeframe == 5))
          pImpl->selectedTimeframe = 5;
        if (ImGui::MenuItem("15 Minutes", "15", pImpl->selectedTimeframe == 15))
          pImpl->selectedTimeframe = 15;
        if (ImGui::MenuItem("1 Hour", "60", pImpl->selectedTimeframe == 60))
          pImpl->selectedTimeframe = 60;
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Chart Type")) {
        if (ImGui::MenuItem("Candlestick", "C", pImpl->selectedChartType == 0))
          pImpl->selectedChartType = 0;
        if (ImGui::MenuItem("Volume", "V", pImpl->selectedChartType == 1))
          pImpl->selectedChartType = 1;
        if (ImGui::MenuItem("Footprint", "F", pImpl->selectedChartType == 2))
          pImpl->selectedChartType = 2;
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("View")) {
        if (ImGui::MenuItem("Fit to Data", "F"))
          pImpl->camera->fitToData(
              pImpl->chartDataManager->getTimeRange().first,
              pImpl->chartDataManager->getTimeRange().second,
              pImpl->chartDataManager->getPriceRange().first,
              pImpl->chartDataManager->getPriceRange().second);
        ImGui::EndMenu();
      }
      ImGui::EndMenuBar();
    }

    // Get the drawing area inside the window
    ImVec2 chartMin = ImGui::GetCursorScreenPos();
    ImVec2 chartMax = ImGui::GetContentRegionMax();
    float chartWidth = chartMax.x - chartMin.x;
    float chartHeight = chartMax.y - chartMin.y - 30; // Leave room for status bar

    // Update camera chart area
    pImpl->camera->setChartArea(chartMin.x, chartMin.y, chartWidth, chartHeight);

    // Auto-fit on first data
    auto timeRange = pImpl->chartDataManager->getTimeRange();
    if (timeRange.first == 0 && timeRange.second == 0) {
      // No data yet - wait
    } else {
      // Check if camera needs initial fit
      auto camRange = pImpl->camera->getTimeRange();
      if (camRange.first == 0 && camRange.second == 0) {
        pImpl->camera->fitToData(timeRange.first, timeRange.second,
                                 pImpl->chartDataManager->getPriceRange().first,
                                 pImpl->chartDataManager->getPriceRange().second);
      }
    }

    // Update chart type
    pImpl->chartRenderer->setChartType(
        static_cast<ChartType>(pImpl->selectedChartType));

    // Render the chart
    pImpl->chartRenderer->render((int)io.DisplaySize.x, (int)io.DisplaySize.y,
                                 *pImpl->camera);

    // Set cursor for pan/zoom
    if (pImpl->isDragging) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    // Status bar
    ImGui::SetCursorScreenPos(ImVec2(chartMin.x, chartMax.y - 25));
    ImGui::Separator();
    ImGui::Text("Price: %.2f | Timeframe: %d min | Chart: %s | Pan: Drag | Zoom: Scroll",
                pImpl->chartDataManager->getCurrentCandle().close > 0 
                    ? pImpl->chartDataManager->getCurrentCandle().close : 0.0,
                pImpl->selectedTimeframe,
                pImpl->selectedChartType == 0 ? "Candlestick" : 
                pImpl->selectedChartType == 1 ? "Volume" : "Footprint");

    ImGui::End();

    // ===== TICK DATA WINDOW =====
    ImGui::Begin("Live Ticks");
    ImGui::Text("Real-time Trade Feed:");

    // Show last 20 ticks in a table
    auto candles = pImpl->chartDataManager->getCandles();
    if (!candles.empty()) {
      ImGui::Separator();
      ImGui::Text("Latest Candle (M%d):", pImpl->selectedTimeframe);
      const auto &candle = candles.back();
      ImGui::Text("O: %.2f  H: %.2f  L: %.2f  C: %.2f  Vol: %.4f",
                  candle.open, candle.high, candle.low, candle.close, candle.volume);
      ImGui::Text("Start: %llu  End: %llu",
                  (unsigned long long)candle.start_time_ms,
                  (unsigned long long)candle.end_time_ms);
    }

    const auto &currentCandle = pImpl->chartDataManager->getCurrentCandle();
    if (currentCandle.volume > 0) {
      ImGui::Separator();
      ImGui::Text("Current Candle:");
      ImGui::Text("O: %.2f  H: %.2f  L: %.2f  C: %.2f  Vol: %.4f",
                  currentCandle.open, currentCandle.high, currentCandle.low,
                  currentCandle.close, currentCandle.volume);
    }

    ImGui::End();

    ImGui::Render();
    glViewport(0, 0, (int)io.DisplaySize.x, (int)io.DisplaySize.y);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

    if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      SDL_Window *backup_current_window = SDL_GL_GetCurrentWindow();
      SDL_GLContext backup_current_context = SDL_GL_GetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      SDL_GL_MakeCurrent(backup_current_window, backup_current_context);
    }

    SDL_GL_SwapWindow(pImpl->window);
  }
}

void MainWindow::updateSymbolData(const core::SymbolData &data) {
  // Can be used to load historical data
}

void MainWindow::addRawTick(const core::Tick &tick) {
  if (pImpl) {
    // Add to chart data manager (aggregates into candles)
    pImpl->chartDataManager->addTick(tick);
  }
}

} // namespace render
} // namespace glora
