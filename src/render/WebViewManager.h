#pragma once

#include <memory>
#include <string>
#include <functional>
#include <vector>
#include <nlohmann/json.hpp>

// Platform detection
#if defined(_WIN32)
#define USE_WEBVIEW2 1
#elif defined(__linux__) || defined(__APPLE__)
#define USE_CEF 1
#endif

namespace glora {
namespace render {

using json = nlohmann::json;

/**
 * IPCMessage - Structured message for IPC communication
 */
struct IPCMessage {
    std::string type;      // Message type: "subscribe", "unsubscribe", "candle", "tick", "error"
    std::string symbol;    // Trading symbol (e.g., "BTCUSDT")
    std::string interval;  // Time interval for candles (e.g., "1m", "5m", "1h")
    int limit = 100;       // Number of candles to fetch
    
    // Candle data
    uint64_t time = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
    double volume = 0.0;
    
    // Error info
    std::string errorMessage;
    
    // Convert to JSON
    json toJson() const;
    
    // Parse from JSON
    static IPCMessage fromJson(const json& j);
    static IPCMessage parse(const std::string& jsonString);
};

/**
 * WebViewManager - Handles embedded web view for UI rendering
 * 
 * Uses WebView2 on Windows and CEF on Linux/macOS for cross-platform
 * web-based UI rendering with IPC communication support.
 */
class WebViewManager {
public:
    using MessageCallback = std::function<void(const std::string& message)>;
    
    struct Config {
        int width = 800;
        int height = 600;
        bool transparentBackground = false;
        bool devToolsEnabled = true;
        std::string defaultUrl;
    };

    WebViewManager();
    ~WebViewManager();

    /**
     * Initialize the WebView with the given configuration
     * @param windowHandle Native window handle (HWND on Windows, void* on Linux)
     * @param config WebView configuration
     * @return true if initialization successful
     */
    bool initialize(void* windowHandle, const Config& config);

    /**
     * Load a local HTML file
     * @param htmlPath Relative or absolute path to HTML file
     * @return true if load successful
     */
    bool loadHTML(const std::string& htmlPath);

    /**
     * Load a URL
     * @param url URL to load
     * @return true if load successful
     */
    bool loadURL(const std::string& url);

    /**
     * Send a message to the JavaScript side
     * @param message JSON message string
     */
    void sendMessage(const std::string& message);
    
    /**
     * Send structured IPC message to frontend
     * @param message IPCMessage to send
     */
    void sendToFrontend(const IPCMessage& message);
    
    /**
     * Send raw JSON string to frontend (convenience method)
     * @param jsonString JSON message string
     */
    void sendToFrontend(const std::string& jsonString);

    /**
     * Register a callback for messages from JavaScript
     * @param callback Function to handle incoming messages
     */
    void setMessageCallback(MessageCallback callback);

    /**
     * Execute JavaScript in the WebView context
     * @param script JavaScript code to execute
     */
    void executeScript(const std::string& script);

    /**
     * Update the WebView (process events, render)
     */
    void update();

    /**
     * Resize the WebView
     * @param width New width
     * @param height New height
     */
    void resize(int width, int height);

    /**
     * Show/hide the WebView
     * @param visible Visibility state
     */
    void setVisible(bool visible);

    /**
     * Check if WebView is initialized and ready
     */
    bool isReady() const;

    /**
     * Shutdown the WebView
     */
    void shutdown();

    /**
     * Get the WebView texture ID for rendering (if using texture rendering)
     * @return OpenGL texture ID or 0 if not using texture
     */
    unsigned int getTextureID() const;

    /**
     * Check if WebView is using texture-based rendering
     */
    bool isTextureBased() const;

private:
    struct Impl;
    std::unique_ptr<Impl> pImpl;
};

// Factory function for creating platform-specific WebViewManager
std::unique_ptr<WebViewManager> createWebViewManager();

} // namespace render
} // namespace glora
