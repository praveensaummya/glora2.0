# Cross-Platform Deployment Plan

## Overview

The application needs to run seamlessly on Windows, Linux, and Android with native performance. Using **CMake** combined with **Qt 6** is the most robust way to achieve this for a C++ application.

## 1. Project Structure

Define a modular CMake project:

```text
/
├── CMakeLists.txt         # Root CMake (Project definition)
├── src/
│   ├── core/              # Shared logic, threading, data models
│   ├── network/           # API interactions, WebSockets
│   ├── render/            # Custom OpenGL/Vulkan shaders and rendering logic
│   └── app/               # Main entry points
├── ui/                    # Qt QML files or standard UI elements
├── plan/                  # Documentation and plan files
└── third_party/           # Submodules (Boost, simdjson, etc.)
```

## 2. Desktop (Windows & Linux)

* **Windows:**
  * Compiler: MSVC (Visual Studio 2022) or LLVM/Clang.
  * Build: `cmake -B build -G "Visual Studio 17 2022" -A x64`
* **Linux:**
  * Compiler: GCC 11+ or Clang 14+.
  * Build: `cmake -B build -G Ninja && cmake --build build`

## 3. Mobile (Android)

* **Prerequisites:** Android SDK, Android NDK, JDK.
* **Qt Android Integration:** Qt provides tools (`androiddeployqt`) that automate bundling the C++ compiled `.so` libraries, assets, and QML files into a deployed `.apk` or `.aab` file.
* **CMake Configuration:**
    Cross-compiling for Android uses the NDK toolchain file:

    ```bash
    cmake -B build_android \
      -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_ROOT/build/cmake/android.toolchain.cmake \
      -DANDROID_ABI=arm64-v8a \
      -DANDROID_PLATFORM=android-24 \
      -DQT_HOST_PATH=/path/to/qt/host
    ```

* **UI Adapation:** Ensure that touch events are intercepted correctly in the rendering engine for pinch-to-zoom and swiping on charts.

## 4. CI/CD Pipeline

* Use **GitHub Actions** or **GitLab CI**.
* **Windows Job:** Runs MSBuild, generates a `.zip` or installer (NSIS).
* **Linux Job:** Runs GCC/Ninja, builds an AppImage or `.deb` package.
* **Android Job:** Sets up the Android SDK/NDK environment, builds the project, signs the `.apk`, and outputs it as an artifact.
