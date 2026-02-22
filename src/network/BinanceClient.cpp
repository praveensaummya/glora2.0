#include "BinanceClient.h"
#include <iostream>
#include <ixwebsocket/IXNetSystem.h>
#include <ixwebsocket/IXWebSocket.h>

namespace glora {
namespace network {

struct BinanceClient::Impl {
  ix::WebSocket webSocket;
  std::string activeSymbol;
  OnTickCallback onTick;
};

BinanceClient::BinanceClient() : pImpl(std::make_unique<Impl>()) {}

BinanceClient::~BinanceClient() { shutdown(); }

bool BinanceClient::initialize() {
  ix::initNetSystem();
  return true;
}

void BinanceClient::fetchHistoricalAggTrades(
    const std::string &symbol, uint64_t startTime, uint64_t endTime,
    std::function<void(const std::vector<core::Tick> &)> onDataCallback) {
  // TODO: Implement HTTP GET request for historical data
  // For now, we'll focus on the Websocket part.
  std::cout << "fetchHistoricalAggTrades called for " << symbol << std::endl;
}

void BinanceClient::subscribeAggTrades(const std::string &symbol,
                                       OnTickCallback callback) {

  pImpl->activeSymbol = symbol;
  pImpl->onTick = std::move(callback);

  // Binance websocket format for aggTrades is
  // wss://stream.binance.com:9443/ws/<symbol>@aggTrade
  std::string lowerSymbol = symbol;
  for (auto &c : lowerSymbol)
    c = std::tolower(c);

  std::string url =
      "wss://stream.binance.com:9443/ws/" + lowerSymbol + "@aggTrade";
  pImpl->webSocket.setUrl(url);

  // Setup message handler
  pImpl->webSocket.setOnMessageCallback(
      [this](const ix::WebSocketMessagePtr &msg) {
        if (msg->type == ix::WebSocketMessageType::Message) {
          try {
            auto j = json::parse(msg->str);

            if (j.contains("e") && j["e"] == "aggTrade") {
              core::Tick tick;
              tick.timestamp_ms = j["T"].get<uint64_t>();
              tick.price = std::stod(j["p"].get<std::string>());
              tick.quantity = std::stod(j["q"].get<std::string>());
              tick.is_buyer_maker = j["m"].get<bool>();

              if (pImpl->onTick) {
                pImpl->onTick(tick);
              }
            }
          } catch (const json::parse_error &e) {
            std::cerr << "JSON Parse error: " << e.what()
                      << "\nMessage: " << msg->str << std::endl;
          } catch (const std::exception &e) {
            std::cerr << "Error parsing tick: " << e.what() << std::endl;
          }
        } else if (msg->type == ix::WebSocketMessageType::Open) {
          std::cout << "Connected to Binance Websocket: " << pImpl->activeSymbol
                    << std::endl;
        } else if (msg->type == ix::WebSocketMessageType::Error) {
          std::cerr << "Websocket Error: " << msg->errorInfo.reason
                    << std::endl;
        }
      });
}

void BinanceClient::connectAndRun() {
  if (!pImpl->activeSymbol.empty()) {
    std::cout << "Starting websocket connection..." << std::endl;
    // start() runs automatically in a background thread provided by ixwebsocket
    pImpl->webSocket.start();
  }
}

void BinanceClient::shutdown() {
  std::cout << "Shutting down Binance Client..." << std::endl;
  pImpl->webSocket.stop();
  ix::uninitNetSystem();
}

} // namespace network
} // namespace glora
