#include "MainWindow.h"
#include "ChartRenderer.h"
#include "Camera.h"
#include "ChartData.h"
#include "WebViewManager.h"
#include "../network/WebSocketServer.h"
#include "../core/ChartDataManager.h"
#include "../network/BinanceClient.h"
#include <SDL2/SDL.h>
#include <SDL2/SDL_opengl.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl2.h>
#include <imgui.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <limits>
#include <filesystem>
#include <chrono>

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
  std::shared_ptr<ChartInteractionHandler> interactionHandler;
  ChartData chartData;  // For interaction handler

  // WebView component
  std::shared_ptr<WebViewManager> webViewManager;
  std::shared_ptr<network::WebSocketServer> wsServer;
  bool useWebView = false;  // Toggle between ImGui and WebView
  
  // Binance client for real-time data
  std::shared_ptr<network::BinanceClient> binanceClient;
  std::string currentTradingSymbol = "BTCUSDT";
  std::string currentInterval = "1m";
  bool isSubscribed = false;

  // Mouse state
  bool isDragging = false;
  bool isDraggingTimeScale = false;
  bool isDraggingPriceScale = false;
  double lastMouseX = 0;
  double lastMouseY = 0;
  double mouseWheelAccum = 0;
  bool crosshairEnabled = true;

  // Axis interaction areas (in pixels from edge)
  const double timeScaleHeight = 30.0;  // Bottom time scale area
  const double priceScaleWidth = 70.0;   // Right price scale area
  
  // Current chart area (updated each frame)
  double chartAreaX = 0;
  double chartAreaY = 0;
  double chartAreaWidth = 800;
  double chartAreaHeight = 600;

  // Hover state for tooltip
  bool showTooltip = false;
  core::Candle hoveredCandle;
  double hoveredPrice = 0;
  uint64_t hoveredTime = 0;

  // UI state
  int selectedTimeframe = 1; // 1 = M1, 5 = M5, etc.
  int selectedChartType = 0; // 0 = Candlestick, 1 = Volume, 2 = Footprint
  std::string currentSymbol = "BTCUSDT";

  // Price data for status bar
  double lastClose = 0;
  double dayHigh = 0;
  double dayLow = 0;
  double dayOpen = 0;
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
  pImpl->interactionHandler = std::make_shared<ChartInteractionHandler>();
  
  // Initialize WebViewManager (disabled by default for now)
  pImpl->webViewManager = std::make_shared<WebViewManager>();
  
  // Initialize WebSocket server for frontend communication
  pImpl->wsServer = std::make_shared<network::WebSocketServer>(8080);
  pImpl->wsServer->start();
  
  // Initialize Binance client
  pImpl->binanceClient = std::make_shared<network::BinanceClient>();
  pImpl->binanceClient->initialize();
}

MainWindow::~MainWindow() {
  // Shutdown WebView if initialized
  if (pImpl->webViewManager) {
    pImpl->webViewManager->shutdown();
  }
  
  // Shutdown WebSocket server
  if (pImpl->wsServer) {
    pImpl->wsServer->stop();
  }
  
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

  // Decide GL+GLSL version
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

  // Initialize interaction handler
  pImpl->interactionHandler->registerForSync(pImpl->currentSymbol);
  pImpl->interactionHandler->setSnapMode(SnapMode::ALL);

  // Initialize WebView (optional - can be enabled via environment variable or config)
  // Currently disabled by default - enable with USE_WEBVIEW env var
  const char* useWebViewEnv = std::getenv("USE_WEBVIEW");
  if (useWebViewEnv && std::string(useWebViewEnv) == "1") {
    pImpl->useWebView = true;
    
    WebViewManager::Config webConfig;
    webConfig.width = pImpl->width;
    webConfig.height = pImpl->height;
    webConfig.devToolsEnabled = true;
    
    // Get the executable path to locate web resources
    std::filesystem::path execPath = std::filesystem::current_path();
    std::filesystem::path webPath = execPath / "web" / "index.html";
    
    if (std::filesystem::exists(webPath)) {
      std::string url = "file://" + webPath.string();
#if defined(_WIN32)
      std::replace(url.begin(), url.end(), '\\', '/');
#endif
      webConfig.defaultUrl = url;
      
      // Initialize WebView with SDL window handle
      void* windowHandle = reinterpret_cast<void*>(pImpl->window);
      pImpl->webViewManager->initialize(windowHandle, webConfig);
      
      // Set up IPC message callback to handle messages from React
      pImpl->webViewManager->setMessageCallback([this](const std::string& message) {
        this->handleIPCMessage(message);
      });
      
      std::cout << "WebView initialized with: " << url << std::endl;
    } else {
      std::cerr << "Web resources not found at: " << webPath << std::endl;
      pImpl->useWebView = false;
    }
  }

  return true;
}

// Helper function to format timestamp
std::string formatTime(uint64_t timestamp_ms) {
  std::time_t time = static_cast<std::time_t>(timestamp_ms / 1000);
  std::tm *tm = std::localtime(&time);
  std::stringstream ss;
  ss << std::put_time(tm, "%Y-%m-%d %H:%M");
  return ss.str();
}

// Helper function to format price with appropriate precision
std::string formatPrice(double price) {
  if (price >= 1000) {
    return std::to_string(static_cast<int>(price));
  } else if (price >= 1) {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(2) << price;
    return ss.str();
  } else {
    std::stringstream ss;
    ss << std::fixed << std::setprecision(6) << price;
    return ss.str();
  }
}

void MainWindow::run() {
  ImGuiIO &io = ImGui::GetIO();
  pImpl->done = false;

  while (!pImpl->done) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
      ImGui_ImplSDL2_ProcessEvent(&event);

      // Handle keyboard shortcuts
      if (event.type == SDL_KEYDOWN) {
        // Timeframe shortcuts (1-8)
        if (event.key.keysym.sym == SDLK_1) {
          pImpl->selectedTimeframe = 1;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::M1);
        } else if (event.key.keysym.sym == SDLK_2) {
          pImpl->selectedTimeframe = 5;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::M5);
        } else if (event.key.keysym.sym == SDLK_3) {
          pImpl->selectedTimeframe = 15;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::M15);
        } else if (event.key.keysym.sym == SDLK_4) {
          pImpl->selectedTimeframe = 60;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::H1);
        } else if (event.key.keysym.sym == SDLK_5) {
          pImpl->selectedTimeframe = 120;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::H1); // Using H1 as placeholder
        } else if (event.key.keysym.sym == SDLK_6) {
          pImpl->selectedTimeframe = 240;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::H4);
        } else if (event.key.keysym.sym == SDLK_7) {
          pImpl->selectedTimeframe = 1440;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::D1);
        } else if (event.key.keysym.sym == SDLK_8) {
          pImpl->selectedTimeframe = 10080;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::D1); // Using D1 as placeholder
        }
        // Zoom shortcuts (+/- or =/-)
        else if (event.key.keysym.sym == SDLK_EQUALS || event.key.keysym.sym == SDLK_KP_PLUS) {
          // Zoom in
          pImpl->camera->zoom(0.9, 0.5, 0.5);
        } else if (event.key.keysym.sym == SDLK_MINUS || event.key.keysym.sym == SDLK_KP_MINUS) {
          // Zoom out
          pImpl->camera->zoom(1.1, 0.5, 0.5);
        }
        // Navigation shortcuts (Home/End)
        else if (event.key.keysym.sym == SDLK_HOME) {
          // Jump to start of data
          auto timeRange = pImpl->chartDataManager->getTimeRange();
          if (timeRange.first > 0) {
            auto [minPrice, maxPrice] = pImpl->chartDataManager->getPriceRange();
            pImpl->camera->fitToData(timeRange.first, timeRange.first + (timeRange.second - timeRange.first),
                                     minPrice, maxPrice);
          }
        } else if (event.key.keysym.sym == SDLK_END) {
          // Jump to end of data
          auto timeRange = pImpl->chartDataManager->getTimeRange();
          if (timeRange.second > 0) {
            auto [minPrice, maxPrice] = pImpl->chartDataManager->getPriceRange();
            pImpl->camera->fitToData(timeRange.second - (timeRange.second - timeRange.first), timeRange.second,
                                     minPrice, maxPrice);
          }
        }
        // Toggle crosshair with 'C' key
        else if (event.key.keysym.sym == SDLK_c) {
          pImpl->crosshairEnabled = !pImpl->crosshairEnabled;
          if (!pImpl->crosshairEnabled) {
            pImpl->interactionHandler->hideCrosshair();
          } else {
            pImpl->interactionHandler->showCrosshair();
          }
        }
        // Open settings with Ctrl+,
        else if ((event.key.keysym.mod & KMOD_CTRL) && event.key.keysym.sym == SDLK_COMMA) {
          // Open settings - this would trigger a settings dialog
          std::cout << "Settings shortcut triggered" << std::endl;
        }
      }

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
          
          // Check if mouse is in price scale area (right side of chart)
          double mouseRelX = mouseX - pImpl->chartAreaX;
          bool inPriceScaleArea = (mouseRelX > pImpl->chartAreaWidth - pImpl->priceScaleWidth) && 
                                  (mouseX >= pImpl->chartAreaX && mouseX <= pImpl->chartAreaX + pImpl->chartAreaWidth) &&
                                  (mouseY >= pImpl->chartAreaY && mouseY <= pImpl->chartAreaY + pImpl->chartAreaHeight);
          
          // Check if mouse is in time scale area (bottom of chart)
          double mouseRelY = mouseY - pImpl->chartAreaY;
          bool inTimeScaleArea = (mouseRelY > pImpl->chartAreaHeight - pImpl->timeScaleHeight) &&
                                (mouseX >= pImpl->chartAreaX && mouseX <= pImpl->chartAreaX + pImpl->chartAreaWidth) &&
                                (mouseY >= pImpl->chartAreaY && mouseY <= pImpl->chartAreaY + pImpl->chartAreaHeight);
          
          if (inPriceScaleArea) {
            // Only zoom price (Y-axis)
            pImpl->camera->zoomPrice(zoomFactor, normY);
          } else if (inTimeScaleArea) {
            // Only zoom time (X-axis)
            pImpl->camera->zoomTime(zoomFactor, normX);
          } else {
            // Normal zoom - both axes
            pImpl->camera->zoom(zoomFactor, normX, normY);
          }
          
          pImpl->mouseWheelAccum = 0;
        }
      }

      // Handle mouse drag for pan
      if (event.type == SDL_MOUSEBUTTONDOWN) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          pImpl->isDragging = true;
          pImpl->lastMouseX = event.button.x;
          pImpl->lastMouseY = event.button.y;
          
          // Check if clicking on time scale (bottom area) or price scale (right area)
          double mouseRelX = event.button.x - pImpl->chartAreaX;
          double mouseRelY = event.button.y - pImpl->chartAreaY;
          
          // Time scale area: bottom 30 pixels of chart
          if (mouseRelY > pImpl->chartAreaHeight - pImpl->timeScaleHeight && 
              mouseRelX > 0 && mouseRelX < pImpl->chartAreaWidth) {
            pImpl->isDraggingTimeScale = true;
          }
          // Price scale area: right 70 pixels of chart
          else if (mouseRelX > pImpl->chartAreaWidth - pImpl->priceScaleWidth && 
                   mouseRelY > 0 && mouseRelY < pImpl->chartAreaHeight) {
            pImpl->isDraggingPriceScale = true;
          }
        }
      }

      if (event.type == SDL_MOUSEBUTTONUP) {
        if (event.button.button == SDL_BUTTON_LEFT) {
          pImpl->isDragging = false;
          pImpl->isDraggingTimeScale = false;
          pImpl->isDraggingPriceScale = false;
        }
      }

      if (event.type == SDL_MOUSEMOTION && pImpl->isDragging) {
        double deltaX = (event.motion.x - pImpl->lastMouseX) / io.DisplaySize.x * 2.0;
        double deltaY = (event.motion.y - pImpl->lastMouseY) / io.DisplaySize.y * 2.0;
        
        // Axis-specific panning
        if (pImpl->isDraggingTimeScale) {
          // Only pan time (horizontal)
          pImpl->camera->panTime(-deltaX);
        } else if (pImpl->isDraggingPriceScale) {
          // Only pan price (vertical)
          pImpl->camera->panPrice(deltaY);
        } else {
          // Normal pan - both axes
          pImpl->camera->pan(-deltaX, deltaY);
        }
        
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
        if (ImGui::MenuItem("1 Minute", "1", pImpl->selectedTimeframe == 1)) {
          pImpl->selectedTimeframe = 1;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::M1);
        }
        if (ImGui::MenuItem("5 Minutes", "5", pImpl->selectedTimeframe == 5)) {
          pImpl->selectedTimeframe = 5;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::M5);
        }
        if (ImGui::MenuItem("15 Minutes", "15", pImpl->selectedTimeframe == 15)) {
          pImpl->selectedTimeframe = 15;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::M15);
        }
        if (ImGui::MenuItem("1 Hour", "60", pImpl->selectedTimeframe == 60)) {
          pImpl->selectedTimeframe = 60;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::H1);
        }
        if (ImGui::MenuItem("4 Hours", "240", pImpl->selectedTimeframe == 240)) {
          pImpl->selectedTimeframe = 240;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::H4);
        }
        if (ImGui::MenuItem("1 Day", "D", pImpl->selectedTimeframe == 1440)) {
          pImpl->selectedTimeframe = 1440;
          pImpl->chartDataManager->setTimeframe(core::Timeframe::D1);
        }
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

      if (ImGui::BeginMenu("Crosshair")) {
        bool crosshair = pImpl->crosshairEnabled;
        if (ImGui::MenuItem("Enable", "C", crosshair)) {
          pImpl->crosshairEnabled = !pImpl->crosshairEnabled;
          if (pImpl->crosshairEnabled) {
            pImpl->interactionHandler->showCrosshair();
          } else {
            pImpl->interactionHandler->hideCrosshair();
          }
        }
        ImGui::EndMenu();
      }

      if (ImGui::BeginMenu("Settings")) {
        if (ImGui::MenuItem("Preferences", "Ctrl+,", false)) {
          // Open settings dialog
          std::cout << "Opening settings..." << std::endl;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Price Scale", "", false)) {
          // Toggle price scale settings
        }
        if (ImGui::MenuItem("Time Scale", "", false)) {
          // Toggle time scale settings  
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout", "", false)) {
          pImpl->camera->fitToData(
              pImpl->chartDataManager->getTimeRange().first,
              pImpl->chartDataManager->getTimeRange().second,
              pImpl->chartDataManager->getPriceRange().first,
              pImpl->chartDataManager->getPriceRange().second);
        }
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
    
    // Store chart area for mouse interaction
    pImpl->chartAreaX = chartMin.x;
    pImpl->chartAreaY = chartMin.y;
    pImpl->chartAreaWidth = chartWidth;
    pImpl->chartAreaHeight = chartHeight;

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

    // ===== CROSSHAIR =====
    if (pImpl->crosshairEnabled) {
      ImDrawList *drawList = ImGui::GetWindowDrawList();
      
      // Get mouse position relative to chart
      ImVec2 mousePos = ImGui::GetMousePos();
      double mouseX = mousePos.x - chartMin.x;
      double mouseY = mousePos.y - chartMin.y;

      // Check if mouse is within chart area
      if (mouseX >= 0 && mouseX <= chartWidth && mouseY >= 0 && mouseY <= chartHeight) {
        // Update interaction handler with crosshair position
        pImpl->interactionHandler->setCrosshairPosition(mousePos.x, mousePos.y,
                                                          *pImpl->camera,
                                                          pImpl->chartData);
        
        const auto& crosshair = pImpl->interactionHandler->getCrosshair();
        
        // Draw crosshair lines
        ImU32 crosshairColor = IM_COL32(100, 150, 200, 200);
        drawList->AddLine(ImVec2(chartMin.x, mousePos.y), 
                         ImVec2(chartMin.x + chartWidth, mousePos.y),
                         crosshairColor, 1.0f);
        drawList->AddLine(ImVec2(mousePos.x, chartMin.y), 
                         ImVec2(mousePos.x, chartMin.y + chartHeight),
                         crosshairColor, 1.0f);

        // Draw price label on right side
        auto [time, price] = pImpl->camera->screenToChart(mousePos.x, mousePos.y, 1, 1);
        std::string priceLabel = formatPrice(price);
        drawList->AddRectFilled(ImVec2(chartMin.x + chartWidth - 70, mousePos.y - 10),
                                ImVec2(chartMin.x + chartWidth, mousePos.y + 10),
                                IM_COL32(50, 50, 70, 200), 2.0f);
        drawList->AddText(ImVec2(chartMin.x + chartWidth - 65, mousePos.y - 7),
                         IM_COL32(200, 200, 200, 255), priceLabel.c_str());

        // Draw time label at bottom
        std::string timeLabel = formatTime(time);
        drawList->AddRectFilled(ImVec2(mousePos.x - 50, chartMin.y + chartHeight - 20),
                                ImVec2(mousePos.x + 50, chartMin.y + chartHeight),
                                IM_COL32(50, 50, 70, 200), 2.0f);
        drawList->AddText(ImVec2(mousePos.x - 40, chartMin.y + chartHeight - 17),
                         IM_COL32(200, 200, 200, 255), timeLabel.c_str());
      }
    }

    // ===== CURRENT PRICE LINE =====
    {
      ImDrawList *drawList = ImGui::GetWindowDrawList();
      
      const auto& currentCandle = pImpl->chartDataManager->getCurrentCandle();
      auto candles = pImpl->chartDataManager->getCandles();
      
      double currentPrice = 0;
      if (currentCandle.volume > 0) {
        currentPrice = currentCandle.close;
      } else if (!candles.empty()) {
        currentPrice = candles.back().close;
      }
      
      if (currentPrice > 0) {
        auto [minPrice, maxPrice] = pImpl->camera->getPriceRange();
        double priceRange = maxPrice - minPrice;
        
        if (priceRange > 0) {
          float priceY = chartMin.y + static_cast<float>((1.0 - (currentPrice - minPrice) / priceRange) * chartHeight);
          
          // Draw current price line
          ImU32 priceLineColor = IM_COL32(255, 200, 50, 200);
          drawList->AddLine(ImVec2(chartMin.x, priceY), 
                           ImVec2(chartMin.x + chartWidth, priceY),
                           priceLineColor, 1.5f);
          
          // Draw price label box
          std::string priceLabel = formatPrice(currentPrice);
          float labelWidth = 75;
          drawList->AddRectFilled(ImVec2(chartMin.x + chartWidth - labelWidth, priceY - 10),
                                  ImVec2(chartMin.x + chartWidth, priceY + 10),
                                  IM_COL32(255, 200, 50, 200), 2.0f);
          drawList->AddText(ImVec2(chartMin.x + chartWidth - labelWidth + 5, priceY - 7),
                           IM_COL32(0, 0, 0, 255), priceLabel.c_str());
        }
      }
    }

    // ===== HOVER TOOLTIP =====
    {
      ImVec2 mousePos = ImGui::GetMousePos();
      double mouseX = mousePos.x - chartMin.x;
      double mouseY = mousePos.y - chartMin.y;
      
      pImpl->showTooltip = false;
      
      // Check if mouse is within chart area
      if (mouseX >= 0 && mouseX <= chartWidth && mouseY >= 0 && mouseY <= chartHeight) {
        auto [time, price] = pImpl->camera->screenToChart(mousePos.x, mousePos.y, 1, 1);
        
        // Find the candle at this position
        auto candles = pImpl->chartDataManager->getCandles();
        const auto& currentCandle = pImpl->chartDataManager->getCurrentCandle();
        
        for (const auto& candle : candles) {
          if (time >= candle.start_time_ms && time <= candle.end_time_ms) {
            pImpl->showTooltip = true;
            pImpl->hoveredCandle = candle;
            pImpl->hoveredTime = candle.start_time_ms;
            break;
          }
        }
        
        // Check current candle if no historical candle found
        if (!pImpl->showTooltip && currentCandle.volume > 0 && 
            time >= currentCandle.start_time_ms && time <= currentCandle.end_time_ms) {
          pImpl->showTooltip = true;
          pImpl->hoveredCandle = currentCandle;
          pImpl->hoveredTime = currentCandle.start_time_ms;
        }
        
        if (pImpl->showTooltip) {
          // Draw tooltip window
          ImGui::SetNextWindowPos(ImVec2(mousePos.x + 15, mousePos.y + 15));
          ImGui::SetNextWindowBgAlpha(0.9f);
          ImGui::Begin("Tooltip", nullptr, 
                       ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoMove | 
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_Tooltip);
          
          ImGui::Text("Time: %s", formatTime(pImpl->hoveredTime).c_str());
          ImGui::Separator();
          ImGui::Text("Open:  %s", formatPrice(pImpl->hoveredCandle.open).c_str());
          ImGui::Text("High:  %s", formatPrice(pImpl->hoveredCandle.high).c_str());
          ImGui::Text("Low:   %s", formatPrice(pImpl->hoveredCandle.low).c_str());
          ImGui::Text("Close: %s", formatPrice(pImpl->hoveredCandle.close).c_str());
          ImGui::Text("Volume: %.4f", pImpl->hoveredCandle.volume);
          
          ImGui::End();
        }
      }
    }

    // Set cursor for pan/zoom
    if (pImpl->isDragging) {
      ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeAll);
    } else {
      ImGui::SetMouseCursor(ImGuiMouseCursor_Arrow);
    }

    // ===== ENHANCED STATUS BAR =====
    ImGui::SetCursorScreenPos(ImVec2(chartMin.x, chartMax.y - 25));
    ImGui::Separator();
    
    // Calculate 24h statistics
    auto allCandles = pImpl->chartDataManager->getCandles();
    const auto& currentCandle = pImpl->chartDataManager->getCurrentCandle();
    
    double lastClose = 0;
    double day24hChange = 0;
    double high24h = 0;
    double low24h = std::numeric_limits<double>::max();
    
    if (!allCandles.empty()) {
      lastClose = allCandles.back().close;
      
      // Find 24h high/low (assuming M1 candles, last 1440 candles = 24 hours)
      size_t startIdx = allCandles.size() > 1440 ? allCandles.size() - 1440 : 0;
      for (size_t i = startIdx; i < allCandles.size(); i++) {
        if (allCandles[i].high > high24h) high24h = allCandles[i].high;
        if (allCandles[i].low < low24h) low24h = allCandles[i].low;
      }
      
      // Calculate 24h change
      if (allCandles.size() >= 1440 && allCandles[allCandles.size() - 1440].close > 0) {
        day24hChange = ((lastClose - allCandles[allCandles.size() - 1440].close) / allCandles[allCandles.size() - 1440].close) * 100;
      } else if (allCandles.size() >= 2) {
        // Use first candle of the day if less than 24h of data
        day24hChange = ((lastClose - allCandles.front().open) / allCandles.front().open) * 100;
      }
      
      if (low24h == std::numeric_limits<double>::max()) {
        low24h = allCandles.front().low;
      }
    }
    
    // Include current candle in calculations
    if (currentCandle.volume > 0) {
      if (currentCandle.high > high24h) high24h = currentCandle.high;
      if (currentCandle.low < low24h) low24h = currentCandle.low;
    }
    
    // Get zoom level (approximate)
    auto [camMinTime, camMaxTime] = pImpl->camera->getTimeRange();
    auto [dataMinTime, dataMaxTime] = pImpl->chartDataManager->getTimeRange();
    double zoomLevel = 100.0;
    if (dataMaxTime > dataMinTime && camMaxTime > camMinTime) {
      zoomLevel = (dataMaxTime - dataMinTime) * 100.0 / (camMaxTime - camMinTime);
    }
    
    // Get timeframe string
    const char* tfStr = "1m";
    switch (pImpl->selectedTimeframe) {
      case 1: tfStr = "1m"; break;
      case 5: tfStr = "5m"; break;
      case 15: tfStr = "15m"; break;
      case 60: tfStr = "1h"; break;
      case 120: tfStr = "2h"; break;
      case 240: tfStr = "4h"; break;
      case 1440: tfStr = "1D"; break;
      case 10080: tfStr = "1W"; break;
    }
    
    // Build status bar text
    std::string statusText = pImpl->currentSymbol + " | ";
    statusText += "Last: " + formatPrice(lastClose) + " | ";
    
    // Color the change percentage
    ImVec4 changeColor = day24hChange >= 0 ? ImVec4(0.0f, 0.8f, 0.2f, 1.0f) : ImVec4(0.8f, 0.2f, 0.2f, 1.0f);
    std::string changeStr = day24hChange >= 0 ? "+" : "";
    changeStr += std::to_string(day24hChange);
    changeStr += "%";
    
    statusText += "24h: " + changeStr + " | ";
    statusText += "H: " + formatPrice(high24h) + " L: " + formatPrice(low24h) + " | ";
    statusText += "TF: " + std::string(tfStr) + " | ";
    statusText += "Zoom: " + std::to_string(static_cast<int>(zoomLevel)) + "%";
    
    ImGui::Text("%s", statusText.c_str());

    ImGui::End();

    // ===== TICK DATA WINDOW =====
    ImGui::Begin("Live Ticks");
    ImGui::Text("Real-time Trade Feed:");

    // Show last 20 ticks in a table
    auto candlesForWindow = pImpl->chartDataManager->getCandles();
    if (!candlesForWindow.empty()) {
      ImGui::Separator();
      ImGui::Text("Latest Candle (%s):", tfStr);
      const auto &candle = candlesForWindow.back();
      ImGui::Text("O: %.2f  H: %.2f  L: %.2f  C: %.2f  Vol: %.4f",
                  candle.open, candle.high, candle.low, candle.close, candle.volume);
      ImGui::Text("Start: %llu  End: %llu",
                  (unsigned long long)candle.start_time_ms,
                  (unsigned long long)candle.end_time_ms);
    }

    if (currentCandle.volume > 0) {
      ImGui::Separator();
      ImGui::Text("Current Candle:");
      ImGui::Text("O: %.2f  H: %.2f  L: %.2f  C: %.2f  Vol: %.4f",
                  currentCandle.open, currentCandle.high, currentCandle.low,
                  currentCandle.close, currentCandle.volume);
    }

    ImGui::End();

    // ===== KEYBOARD SHORTCUTS HELP =====
    ImGui::Begin("Shortcuts");
    ImGui::Text("Keyboard Shortcuts:");
    ImGui::Separator();
    ImGui::Text("1-8: Switch timeframe");
    ImGui::Text("+/-: Zoom in/out");
    ImGui::Text("Home: Jump to start");
    ImGui::Text("End: Jump to end");
    ImGui::Text("C: Toggle crosshair");
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

void MainWindow::quit() {
  pImpl->done = true;
}

// Handle IPC messages from React frontend
void MainWindow::handleIPCMessage(const std::string& jsonMessage) {
  try {
    auto message = IPCMessage::parse(jsonMessage);
    std::cout << "[IPC] Received message: type=" << message.type 
              << ", symbol=" << message.symbol 
              << ", interval=" << message.interval << std::endl;
    
    if (message.type == "subscribe") {
      subscribeToMarketData(message.symbol, message.interval);
    } else if (message.type == "unsubscribe") {
      unsubscribeFromMarketData();
    } else if (message.type == "history") {
      // Fetch historical data and send to frontend
      if (pImpl->binanceClient && !message.symbol.empty()) {
        uint64_t endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        uint64_t startTime = endTime - (message.limit > 0 ? message.limit : 100) * 60000; // 1 minute candles
        
        pImpl->binanceClient->fetchKlines(
          message.symbol, 
          message.interval.empty() ? "1m" : message.interval,
          startTime,
          endTime,
          [this, message](const std::vector<core::Candle>& candles) {
            // Send historical candles to frontend
            json response;
            response["type"] = "history";
            response["symbol"] = message.symbol;
            response["interval"] = message.interval;
            
            std::vector<json> candleArray;
            for (const auto& c : candles) {
              json candleJson;
              candleJson["time"] = c.start_time_ms / 1000; // Convert to seconds
              candleJson["open"] = c.open;
              candleJson["high"] = c.high;
              candleJson["low"] = c.low;
              candleJson["close"] = c.close;
              candleJson["volume"] = c.volume;
              candleArray.push_back(candleJson);
            }
            response["candles"] = candleArray;
            
            pImpl->webViewManager->sendToFrontend(response.dump());
          }
        );
      }
    } else if (message.type == "getStatus") {
      // Send status response
      json response;
      response["type"] = "status";
      response["status"] = "ok";
      response["symbol"] = pImpl->currentTradingSymbol;
      response["interval"] = pImpl->currentInterval;
      response["subscribed"] = pImpl->isSubscribed;
      pImpl->webViewManager->sendToFrontend(response.dump());
    }
    
  } catch (const std::exception& e) {
    std::cerr << "[IPC] Error handling message: " << e.what() << std::endl;
    
    // Send error response
    json errorResponse;
    errorResponse["type"] = "error";
    errorResponse["errorMessage"] = e.what();
    pImpl->webViewManager->sendToFrontend(errorResponse.dump());
  }
}

// Send candle data to React frontend
void MainWindow::sendCandleToFrontend(const core::Candle& candle, const std::string& symbol) {
  // Create IPC message
  IPCMessage msg;
  msg.type = "candle";
  msg.symbol = symbol;
  msg.time = candle.start_time_ms / 1000; // Convert to seconds
  msg.open = candle.open;
  msg.high = candle.high;
  msg.low = candle.low;
  msg.close = candle.close;
  msg.volume = candle.volume;
  
  // Send via WebView (if available)
  if (pImpl->webViewManager && pImpl->webViewManager->isReady()) {
    pImpl->webViewManager->sendToFrontend(msg);
  }
  
  // Also broadcast via WebSocket to frontend
  if (pImpl->wsServer && pImpl->wsServer->isRunning()) {
    pImpl->wsServer->broadcast(msg.toJson());
  }
}

// Subscribe to market data using bootstrap: history first, then live stream
void MainWindow::subscribeToMarketData(const std::string& symbol, const std::string& interval) {
  if (!pImpl->binanceClient) {
    return;
  }
  
  // Unsubscribe from previous if any
  unsubscribeFromMarketData();
  
  pImpl->currentTradingSymbol = symbol.empty() ? "BTCUSDT" : symbol;
  pImpl->currentInterval = interval.empty() ? "1m" : interval;
  
  std::cout << "[IPC] Bootstrapping " << pImpl->currentTradingSymbol 
            << " @ " << pImpl->currentInterval << " (history then live)" << std::endl;
  
  // Calculate time range for history (last 7 days)
  uint64_t endTime = std::chrono::duration_cast<std::chrono::milliseconds>(
      std::chrono::system_clock::now().time_since_epoch()).count();
  uint64_t startTime = endTime - (7 * 24 * 60 * 60 * 1000); // 7 days
  
  // Use bootstrap: fetch history first, then start live stream
  pImpl->binanceClient->bootstrapHistoryThenStream(
    pImpl->currentTradingSymbol,
    pImpl->currentInterval,
    startTime,
    endTime,
    // History complete callback - send to frontend
    [this](const std::vector<core::Candle>& candles) {
      json response;
      response["type"] = "history";
      response["symbol"] = pImpl->currentTradingSymbol;
      response["interval"] = pImpl->currentInterval;
      
      std::vector<json> candleArray;
      for (const auto& c : candles) {
        json candleJson;
        candleJson["time"] = c.start_time_ms / 1000; // Convert to seconds
        candleJson["open"] = c.open;
        candleJson["high"] = c.high;
        candleJson["low"] = c.low;
        candleJson["close"] = c.close;
        candleJson["volume"] = c.volume;
        candleArray.push_back(candleJson);
      }
      response["candles"] = candleArray;
      response["historyComplete"] = true; // Flag to indicate history is done
      
      // Send history to frontend
      if (pImpl->webViewManager && pImpl->webViewManager->isReady()) {
        pImpl->webViewManager->sendToFrontend(response.dump());
      }
      // Also broadcast via WebSocket
      if (pImpl->wsServer && pImpl->wsServer->isRunning()) {
        pImpl->wsServer->broadcast(response.dump());
      }
      
      pImpl->isSubscribed = true;
      std::cout << "[IPC] Sent " << candles.size() << " historical candles to frontend" << std::endl;
      
      // Send subscribed confirmation
      json subResponse;
      subResponse["type"] = "subscribed";
      subResponse["symbol"] = pImpl->currentTradingSymbol;
      subResponse["interval"] = pImpl->currentInterval;
      if (pImpl->webViewManager && pImpl->webViewManager->isReady()) {
        pImpl->webViewManager->sendToFrontend(subResponse.dump());
      }
      if (pImpl->wsServer && pImpl->wsServer->isRunning()) {
        pImpl->wsServer->broadcast(subResponse.dump());
      }
    },
    // Live tick callback - send to frontend
    [this](const core::Tick& tick) {
      // Convert tick to candle (simplified - in production, you'd aggregate properly)
      core::Candle candle;
      candle.start_time_ms = (tick.timestamp_ms / 60000) * 60000; // Round to minute
      candle.end_time_ms = candle.start_time_ms + 60000;
      candle.open = tick.price;
      candle.high = tick.price;
      candle.low = tick.price;
      candle.close = tick.price;
      candle.volume = tick.quantity;
      
      sendCandleToFrontend(candle, pImpl->currentTradingSymbol);
    }
  );
}

// Unsubscribe from market data
void MainWindow::unsubscribeFromMarketData() {
  if (pImpl->binanceClient && pImpl->isSubscribed) {
    std::cout << "[IPC] Unsubscribing from market data" << std::endl;
    pImpl->binanceClient->shutdown();
    pImpl->isSubscribed = false;
    
    // Re-initialize for future subscriptions
    pImpl->binanceClient->initialize();
    
    // Send confirmation to frontend
    json response;
    response["type"] = "unsubscribed";
    response["symbol"] = pImpl->currentTradingSymbol;
    if (pImpl->webViewManager && pImpl->webViewManager->isReady()) {
      pImpl->webViewManager->sendToFrontend(response.dump());
    }
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
