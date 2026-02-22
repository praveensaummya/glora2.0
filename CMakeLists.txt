cmake_minimum_required(VERSION 3.16)
project(GloraChartingApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find Qt 6 or ImGui later, for now just a basic executable
# Fetch nlohmann_json
include(FetchContent)

FetchContent_Declare(json
  URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
)
FetchContent_MakeAvailable(json)

# Fetch ixwebsocket for WebSockets and HTTP
FetchContent_Declare(ixwebsocket
    GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
    GIT_TAG        master
)
# Disable ixwebsocket extra deps we don't need for a simple client
set(USE_TLS ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(ixwebsocket)

# Fetch Dear ImGui
FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        docking # Use docking branch for advanced UI features
)
FetchContent_MakeAvailable(imgui)

# Find SDL2 and OpenGL
find_package(SDL2 REQUIRED)
find_package(OpenGL REQUIRED)

# ImGui requires us to manually compile its sources
set(IMGUI_SOURCES
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_demo.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_sdl2.cpp
    ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
)

add_executable(GloraChart
    src/main.cpp
    src/network/BinanceClient.cpp
    src/render/MainWindow.cpp
    src/render/ChartRenderer.cpp
    ${IMGUI_SOURCES}
)

target_include_directories(GloraChart PRIVATE 
    ${imgui_SOURCE_DIR} 
    ${imgui_SOURCE_DIR}/backends
)

target_link_libraries(GloraChart PRIVATE 
    nlohmann_json::nlohmann_json 
    ixwebsocket
    ${SDL2_LIBRARIES}
    OpenGL::GL
)
