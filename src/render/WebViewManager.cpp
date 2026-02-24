#include "WebViewManager.h"
#include "../core/DataModels.h"
#include <iostream>
#include <filesystem>

#if defined(USE_WEBVIEW2)
#include <webview.h>
#endif

namespace glora {
namespace render {

// IPCMessage implementation
json IPCMessage::toJson() const {
    json j;
    j["type"] = type;
    j["symbol"] = symbol;
    j["interval"] = interval;
    j["limit"] = limit;
    j["time"] = time;
    j["open"] = open;
    j["high"] = high;
    j["low"] = low;
    j["close"] = close;
    j["volume"] = volume;
    j["errorMessage"] = errorMessage;
    return j;
}

IPCMessage IPCMessage::fromJson(const json& j) {
    IPCMessage msg;
    msg.type = j.value("type", "");
    msg.symbol = j.value("symbol", "");
    msg.interval = j.value("interval", "");
    msg.limit = j.value("limit", 100);
    msg.time = j.value("time", 0);
    msg.open = j.value("open", 0.0);
    msg.high = j.value("high", 0.0);
    msg.low = j.value("low", 0.0);
    msg.close = j.value("close", 0.0);
    msg.volume = j.value("volume", 0.0);
    msg.errorMessage = j.value("errorMessage", "");
    return msg;
}

IPCMessage IPCMessage::parse(const std::string& jsonString) {
    try {
        auto j = json::parse(jsonString);
        return fromJson(j);
    } catch (const std::exception& e) {
        std::cerr << "Failed to parse IPC message: " << e.what() << std::endl;
        return IPCMessage{};
    }
}

// Platform-specific implementation
struct WebViewManager::Impl {
#if defined(USE_WEBVIEW2)
    // WebView2 specific members
    webview::webview* webviewInstance = nullptr;
    bool isInitialized = false;
    MessageCallback messageCallback;
    unsigned int textureID = 0;
    bool useTextureRendering = false;
#else
    // Stub implementation for Linux
    bool isInitialized = false;
    MessageCallback messageCallback;
    unsigned int textureID = 0;
    bool useTextureRendering = false;
    std::string pendingUrl;
#endif

    Config config;
    void* windowHandle = nullptr;
    bool isVisible = true;
};

WebViewManager::WebViewManager() : pImpl(std::make_unique<Impl>()) {
}

WebViewManager::~WebViewManager() {
    shutdown();
}

bool WebViewManager::initialize(void* windowHandle, const Config& config) {
    pImpl->windowHandle = windowHandle;
    pImpl->config = config;

#if defined(USE_WEBVIEW2)
    std::cout << "Initializing WebView2..." << std::endl;
    
    // Create WebView2 instance
    pImpl->webviewInstance = new webview::webview(config.devToolsEnabled, nullptr);
    
    if (!pImpl->webviewInstance) {
        std::cerr << "Failed to create WebView2 instance" << std::endl;
        return false;
    }
    
    // Navigate to initial URL if provided
    if (!config.defaultUrl.empty()) {
        pImpl->webviewInstance->navigate(config.defaultUrl);
    }
    
    pImpl->isInitialized = true;
    std::cout << "WebView2 initialized successfully" << std::endl;
    return true;

#else
    // Linux stub - store the URL for later use
    std::cout << "Initializing WebView (stub)..." << std::endl;
    pImpl->pendingUrl = config.defaultUrl;
    pImpl->isInitialized = true;
    std::cout << "WebView stub initialized. URL: " << pImpl->pendingUrl << std::endl;
    std::cout << "Note: On Linux, the frontend should connect via WebSocket to localhost:8080" << std::endl;
    return true;
#endif
}

bool WebViewManager::loadHTML(const std::string& htmlPath) {
    if (!pImpl->isInitialized) {
        std::cerr << "WebView not initialized" << std::endl;
        return false;
    }

#if defined(USE_WEBVIEW2)
    // Convert to file URL
    std::filesystem::path path(htmlPath);
    std::string absolutePath = std::filesystem::absolute(path).string();
    
#if defined(_WIN32)
    std::replace(absolutePath.begin(), absolutePath.end(), '\\', '/');
#endif
    std::string url = "file:///" + absolutePath;
    
    pImpl->webviewInstance->navigate(url);
    std::cout << "Loading HTML: " << url << std::endl;
    return true;

#else
    std::filesystem::path path(htmlPath);
    std::string absolutePath = std::filesystem::absolute(path).string();
    pImpl->pendingUrl = "file://" + absolutePath;
    std::cout << "Loading HTML (stub): " << pImpl->pendingUrl << std::endl;
    return true;
#endif
}

bool WebViewManager::loadURL(const std::string& url) {
    if (!pImpl->isInitialized) {
        std::cerr << "WebView not initialized" << std::endl;
        return false;
    }

#if defined(USE_WEBVIEW2)
    pImpl->webviewInstance->navigate(url);
    std::cout << "Loading URL: " << url << std::endl;
    return true;

#else
    pImpl->pendingUrl = url;
    std::cout << "Loading URL (stub): " << url << std::endl;
    return true;
#endif
}

void WebViewManager::sendMessage(const std::string& message) {
    // Delegate to sendToFrontend
    sendToFrontend(message);
}

void WebViewManager::sendToFrontend(const IPCMessage& message) {
    sendToFrontend(message.toJson().dump());
}

void WebViewManager::sendToFrontend(const std::string& jsonString) {
    if (!pImpl->isInitialized) {
        return;
    }

#if defined(USE_WEBVIEW2)
    // Escape the JSON string for safe JavaScript execution
    std::string escapedJson = jsonString;
    for (size_t i = 0; i < escapedJson.size(); ++i) {
        if (escapedJson[i] == '\\') {
            escapedJson.insert(escapedJson.begin() + i, '\\');
            ++i;
        } else if (escapedJson[i] == '"') {
            escapedJson.insert(escapedJson.begin() + i, '\\');
            ++i;
        } else if (escapedJson[i] == '\n') {
            escapedJson[i] = '\\';
            escapedJson.insert(escapedJson.begin() + i + 1, 'n');
            ++i;
        }
    }
    
    // Send message via window.glora IPC handler
    std::string jsCode = "window.glora && window.glora.onMessage && window.glora.onMessage(\"" + escapedJson + "\");";
    pImpl->webviewInstance->eval(jsCode);
    std::cout << "[IPC] Sent to frontend: " << jsonString.substr(0, 100) << "..." << std::endl;

#else
    // Linux stub - print to stdout for debugging
    std::cout << "[IPC] Would send to frontend: " << jsonString.substr(0, std::min(jsonString.size(), size_t(100))) << "..." << std::endl;
    // TODO: Implement WebSocket server to send data to frontend
#endif
}

void WebViewManager::setMessageCallback(MessageCallback callback) {
    pImpl->messageCallback = callback;
}

void WebViewManager::executeScript(const std::string& script) {
    if (!pImpl->isInitialized) {
        return;
    }

#if defined(USE_WEBVIEW2)
    pImpl->webviewInstance->eval(script);
#else
    std::cout << "[IPC] Would execute JS: " << script.substr(0, std::min(script.size(), size_t(100))) << "..." << std::endl;
#endif
}

void WebViewManager::update() {
    if (!pImpl->isInitialized) {
        return;
    }

#if defined(USE_WEBVIEW2)
    // WebView2 handles its own message loop
#elif defined(USE_LINUX)
    // TODO: Process WebSocket events if implementing WebSocket server
#endif
}

void WebViewManager::resize(int width, int height) {
    if (!pImpl->isInitialized) {
        return;
    }

#if defined(USE_WEBVIEW2)
    // WebView2 automatically resizes with the window
#endif
    
    pImpl->config.width = width;
    pImpl->config.height = height;
}

void WebViewManager::setVisible(bool visible) {
    pImpl->isVisible = visible;
}

bool WebViewManager::isReady() const {
    return pImpl->isInitialized;
}

void WebViewManager::shutdown() {
    if (!pImpl->isInitialized) {
        return;
    }

#if defined(USE_WEBVIEW2)
    if (pImpl->webviewInstance) {
        delete pImpl->webviewInstance;
        pImpl->webviewInstance = nullptr;
    }
#endif

    pImpl->isInitialized = false;
    std::cout << "WebView shutdown complete" << std::endl;
}

unsigned int WebViewManager::getTextureID() const {
    return pImpl->textureID;
}

bool WebViewManager::isTextureBased() const {
    return pImpl->useTextureRendering;
}

// Factory function
std::unique_ptr<WebViewManager> createWebViewManager() {
    return std::make_unique<WebViewManager>();
}

} // namespace render
} // namespace glora
