#include "WebSocketServer.h"
#include <iostream>
#include <algorithm>

namespace glora {
namespace network {

WebSocketServer::WebSocketServer(int port)
    : port_(port)
    , isRunning_(false) {
}

WebSocketServer::~WebSocketServer() {
    stop();
}

bool WebSocketServer::start() {
    if (isRunning_) {
        std::cout << "[WebSocketServer] Server already running" << std::endl;
        return true;
    }
    
    std::cout << "[WebSocketServer] Starting server on port " << port_ << "..." << std::endl;
    
    // Create WebSocket server
    server_ = std::make_unique<ix::WebSocketServer>(port_);
    
    // Set connection handler
    server_->setOnConnectionCallback([this](std::weak_ptr<ix::WebSocket> webSocket, std::shared_ptr<ix::ConnectionState> state) {
        auto ws = webSocket.lock();
        if (ws) {
            // Generate a simple client ID
            int clientId = ++lastClientId_;
            auto self = this;
            
            // Set message handler for this connection
            ws->setOnMessageCallback([self, clientId, ws](const ix::WebSocketMessagePtr& msg) {
                self->onMessage(clientId, *ws, msg);
            });
            
            self->onConnection(clientId, *ws);
        }
    });
    
    // Start listening
    bool success = server_->listenAndStart();
    if (!success) {
        std::cerr << "[WebSocketServer] Failed to start server" << std::endl;
        return false;
    }
    
    isRunning_ = true;
    std::cout << "[WebSocketServer] Server started successfully on port " << port_ << std::endl;
    std::cout << "[WebSocketServer] Frontend should connect to: ws://localhost:" << port_ << std::endl;
    
    return true;
}

void WebSocketServer::stop() {
    if (!isRunning_) {
        return;
    }
    
    std::cout << "[WebSocketServer] Stopping server..." << std::endl;
    
    if (server_) {
        server_->stop();
        server_.reset();
    }
    
    {
        std::lock_guard<std::mutex> lock(clientsMutex_);
        clients_.clear();
    }
    
    isRunning_ = false;
    std::cout << "[WebSocketServer] Server stopped" << std::endl;
}

void WebSocketServer::broadcast(const std::string& message) {
    if (!isRunning_ || !server_) {
        return;
    }
    
    auto clients = server_->getClients();
    for (auto& client : clients) {
        if (client) {
            client->send(message);
        }
    }
}

void WebSocketServer::broadcast(const json& message) {
    broadcast(message.dump());
}

bool WebSocketServer::isRunning() const {
    return isRunning_;
}

void WebSocketServer::setMessageCallback(MessageCallback callback) {
    messageCallback_ = std::move(callback);
}

size_t WebSocketServer::getClientCount() const {
    if (!server_) return 0;
    return server_->getClients().size();
}

void WebSocketServer::onMessage(int clientId, const ix::WebSocket& webSocket, const ix::WebSocketMessagePtr& msg) {
    if (msg->type == ix::WebSocketMessageType::Message) {
        if (messageCallback_) {
            messageCallback_(msg->str);
        }
    } else if (msg->type == ix::WebSocketMessageType::Error) {
        std::cerr << "[WebSocketServer] Error for client " << clientId << ": " << msg->errorInfo.reason << std::endl;
    }
}

void WebSocketServer::onConnection(int clientId, const ix::WebSocket& webSocket) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.push_back(clientId);
    std::cout << "[WebSocketServer] Client " << clientId << " connected. Total clients: " << clients_.size() << std::endl;
}

void WebSocketServer::onDisconnection(int clientId, const ix::WebSocket& webSocket, int code, const std::string& reason) {
    std::lock_guard<std::mutex> lock(clientsMutex_);
    clients_.erase(std::remove(clients_.begin(), clients_.end(), clientId), clients_.end());
    std::cout << "[WebSocketServer] Client " << clientId << " disconnected. Total clients: " << clients_.size() << std::endl;
}

} // namespace network
} // namespace glora
