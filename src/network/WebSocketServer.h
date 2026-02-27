#pragma once

#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <ixwebsocket/IXWebSocketServer.h>
#include <ixwebsocket/IXWebSocket.h>
#include <nlohmann/json.hpp>
#include "BinarySerialization.h"

namespace glora {
namespace network {

using json = nlohmann::json;

/**
 * WebSocketServer - Simple WebSocket server for broadcasting market data to frontend
 * 
 * Listens on specified port and broadcasts messages to all connected clients.
 */
class WebSocketServer {
public:
    using MessageCallback = std::function<void(const std::string& message)>;
    
    /**
     * @param port Port to listen on (default: 8080)
     */
    explicit WebSocketServer(int port = 8080);
    ~WebSocketServer();
    
    /**
     * Start the WebSocket server
     * @return true if started successfully
     */
    bool start();
    
    /**
     * Stop the WebSocket server
     */
    void stop();
    
    /**
     * Broadcast a message to all connected clients
     * @param message JSON message to broadcast
     */
    void broadcast(const std::string& message);
    
    /**
     * Broadcast a JSON message to all connected clients
     * @param message JSON message to broadcast
     */
    void broadcast(const json& message);
    
    // --- Binary Serialization Support ---
    /**
     * Broadcast binary market data using BinarySerialization
     * More efficient than JSON for high-frequency data
     * @param data Binary message data
     */
    void broadcastBinary(const std::vector<uint8_t>& data);
    
    /**
     * Broadcast candle data as binary
     */
    void broadcastCandle(uint64_t openTime, uint64_t closeTime,
                        double open, double high, double low, double close,
                        double volume, uint32_t trades, bool closed);
    
    /**
     * Broadcast trade data as binary
     */
    void broadcastTrade(int64_t tradeId, double price, double quantity,
                       uint64_t tradeTime, bool isBuyerMaker);
    
    /**
     * Broadcast order book as binary
     */
    void broadcastOrderBook(uint64_t lastUpdateId,
                           const std::vector<std::pair<double, double>>& bids,
                           const std::vector<std::pair<double, double>>& asks);
    
    /**
     * Check if server is running
     * @return true if server is running
     */
    bool isRunning() const;
    
    /**
     * Set callback for incoming messages from clients
     * @param callback Function to handle incoming messages
     */
    void setMessageCallback(MessageCallback callback);
    
    /**
     * Get the number of connected clients
     * @return Number of connected clients
     */
    size_t getClientCount() const;

private:
    void onMessage(int clientId, const ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg);
    void onConnection(int clientId, const ix::WebSocket& webSocket);
    void onDisconnection(int clientId, const ix::WebSocket& webSocket, int code, const std::string& reason);

    int port_;
    std::unique_ptr<ix::WebSocketServer> server_;
    std::vector<int> clients_;
    mutable std::mutex clientsMutex_;
    MessageCallback messageCallback_;
    bool isRunning_;
    int lastClientId_ = 0;
    
    // Binary serializer for efficient market data transmission
    BinarySerializer binarySerializer_;
};

} // namespace network
} // namespace glora
