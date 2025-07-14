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
#include "../include/core/UDPReliabilityProtocol.hpp"
// ... other includes as needed

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

        // Forward raw UDP data to the Connection for decryption, decompression, etc.
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

//
// Additional thread to simulate periodic updates for ACK/retransmissions & to log RTT samples.
//
void ReliabilityUpdateLoop() {
    while (true) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // adjust tick rate as needed
        auto now = std::chrono::steady_clock::now();

        {
            // Lock the connection map to iterate over current connections
            std::scoped_lock lock(g_connectionMutex);
            for (auto& [key, conn] : g_connectionMap) {
                // Get a reference to the connection's reliable state.
                // (Assumes you have a getter; adapt if necessary.)
                auto& state = conn->GetReliableState();

                // Process retransmissions
                UDPReliabilityProtocol::ProcessRetransmissions(
                    state, now,
                    // sendFunc: re-sends packet via connection’s SendPacket() or similar callback.
                    [conn](const std::vector<uint8_t>& packet) {
                        conn->SendPacket(packet);
                    }
                );

                // Check if it's time to send an ACK-only packet.
                if (UDPReliabilityProtocol::ShouldSendAck(state, now)) {
                    // Prepare ACK-only packet(s). Here packetType might be defined as your ACK type.
                    constexpr uint8_t ACK_PACKET_TYPE = 6; // example type value for ACK-only packets
                    auto ackPackets = UDPReliabilityProtocol::PrepareOutgoingPackets(
                        state, nullptr, 0, ACK_PACKET_TYPE, state.nextNonce++
                    );

                    for (auto& pkt : ackPackets) {
                        conn->SendPacket(pkt);
                    }
                }
            }
        }
    }
}

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

    // Start the reliability update thread to process ACKs and retransmissions.
    std::thread reliabilityThread(ReliabilityUpdateLoop);

    // Main server loop remains simple.
    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    reliabilityThread.join();
    return 0;
}
