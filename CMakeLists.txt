cmake_minimum_required(VERSION 3.16)
project(GloraChartingApp VERSION 1.0.0 LANGUAGES CXX)

# Set CMake policies
cmake_policy(SET CMP0135 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt 6 or ImGui later, for now just a basic executable
# Fetch nlohmann_json
include(FetchContent)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Fetch ixwebsocket for WebSockets and HTTP
FetchContent_Declare(ixwebsocket
    URL https://github.com/machinezone/IXWebSocket/archive/refs/tags/v11.4.6.tar.gz
)
# Disable ixwebsocket extra deps we don't need for a simple client
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

# Fetch Dear ImGui
# Using a specific version tag for reliability
FetchContent_Declare(imgui
    URL https://github.com/ocornut/imgui/archive/refs/tags/v1.92.6-docking.tar.gz
)
FetchContent_MakeAvailable(imgui)

# Fetch ImPlot (add-on for ImGui for high-performance charts)
FetchContent_Declare(implot
    URL https://github.com/epezent/implot/archive/refs/tags/v0.17.tar.gz
)
FetchContent_MakeAvailable(implot)

# Find SDL2 and OpenGL
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# Find OpenSSL
find_package(OpenSSL REQUIRED)

# Find SQLite (required for local storage)
find_package(SQLite3 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/src)

# Add ImPlot include directory
if (implot_POPULATED)
  include_directories(${implot_SOURCE_DIR})
endif()

# Add SQLite include directory
include_directories(${SQLite3_INCLUDE_DIRS})

# ImGui requires us to manually compile its sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
    # ImPlot sources
    ${implot_SOURCE_DIR}/implot.cpp
    ${implot_SOURCE_DIR}/implot_items.cpp
)

add_executable(GloraChart
    src/main.cpp
    src/network/BinanceClient.cpp
    src/render/MainWindow.cpp
    src/render/ChartRenderer.cpp
    src/settings/SettingsManager.cpp
    src/database/Database.cpp
    src/core/DataManager.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(GloraChart PRIVATE 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
    ${implot_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(GloraChart PRIVATE 
    nlohmann_json::nlohmann_json 
    ixwebsocket
    ${SDL2_LIBRARIES}
    OpenGL::GL
    OpenSSL::SSL
    OpenSSL::Crypto
    SQLite::SQLite3
)
