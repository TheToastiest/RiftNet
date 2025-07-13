// File: main.cpp

#include <unordered_map>
#include <memory>
#include <csignal>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <iostream>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "../include/core/Logger.hpp"
#include "../include/core/NetworkTypes.hpp"
#include "../include/core/INetworkIOEvents.hpp"
#include "../include/core/INetworkIO.hpp"
#include "../include/core/NetworkEndpoint.hpp"
#include "../include/platform/UDPSocketAsync.hpp"
#include "../include/core/Connection.hpp"
#include "../include/core/HandshakePacket.hpp"
#include "../include/core/SecureChannel.hpp"
#include "../include/core/PacketTypes.hpp"
#include "RiftEncrypt.hpp"
#include "riftcompress.hpp"

using namespace RiftForged::Networking;
using namespace RiftForged::Logging;

static std::unordered_map<std::string, std::shared_ptr<Connection>> g_connectionMap;
static std::mutex g_connectionMutex;

std::unique_ptr<UDPSocketAsync> udpSocket;

class PacketHandler : public INetworkIOEvents {
public:
    void OnRawDataReceived(const NetworkEndpoint& sender,
        const uint8_t* data,
        uint32_t size,
        OverlappedIOContext* context) override {
        std::string key = sender.ToString();
        std::shared_ptr<Connection> conn;

        {
            std::scoped_lock lock(g_connectionMutex);

            if (g_connectionMap.count(key) == 0) {
                auto newConn = std::make_shared<Connection>(sender);
                g_connectionMap[key] = newConn;

                RF_NETWORK_INFO("New connection created for {}", key);

                newConn->SetSendCallback([](const NetworkEndpoint& recipient, const std::vector<uint8_t>& packet) {
                    udpSocket->SendData(recipient, packet.data(), static_cast<uint32_t>(packet.size()));
                    });

                // Immediately send local public key as handshake start
                const auto& publicKey = newConn->GetLocalPublicKey();
                newConn->SendUnencrypted(std::vector<uint8_t>(publicKey.begin(), publicKey.end()));
            }

            conn = g_connectionMap[key];
        }

        // Forward raw UDP data to the Connection to decrypt, decompress, interpret, and possibly respond
        std::vector<uint8_t> payload(data, data + size);
        conn->HandleRawPacket(payload);
    }

    void OnSendCompleted(OverlappedIOContext* context, bool success, uint32_t bytesSent) override {
        context->endpoint = NetworkEndpoint(context->remoteAddrNative);
        const auto targetStr = context->endpoint.ToString();

        if (success)
            RF_NETWORK_DEBUG("Send completed: {} bytes to {}", bytesSent, targetStr);
        else
            RF_NETWORK_ERROR("Send failed to {}", targetStr);
    }

    void OnNetworkError(const std::string& errorMessage, int errorCode = 0) override {
        RF_NETWORK_ERROR("Network error ({}): {}", errorCode, errorMessage);
    }
};

int main() {
    Logger::Init();  // Initializes spdlog with console + file sinks
    udpSocket = std::make_unique<UDPSocketAsync>();
    RF_NETWORK_INFO("=== RiftNet UDP Secure Server Test ===");

    PacketHandler handler;

    if (!udpSocket->Init("0.0.0.0", 7777, &handler)) {
        RF_NETWORK_ERROR("Failed to initialize UDPSocketAsync.");
        return 1;
    }

    if (!udpSocket->Start()) {
        RF_NETWORK_ERROR("Failed to start UDPSocketAsync.");
        return 1;
    }

    RF_NETWORK_INFO("Listening on port 7777. Press Ctrl+C to stop.");

    // Keep server running
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    return 0;
}