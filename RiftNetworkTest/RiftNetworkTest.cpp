// File: RiftNetTest_Server.cpp

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

#include "../RiftNet/include/core/Logger.hpp"
#include "../RiftNet/include/core/NetworkTypes.hpp"
#include "../RiftNet/include/core/INetworkIOEvents.hpp"
#include "../RiftNet/include/core/INetworkIO.hpp"
#include "../RiftNet/include/core/NetworkEndpoint.hpp"
#include "../RiftNet/include/platform/UDPSocketAsync.hpp"
#include "../RiftNet/include/core/Connection.hpp"
#include "../RiftNet/include/core/UDPReliabilityProtocol.hpp"

using namespace RiftForged::Networking;
using namespace RiftForged::Logging;

static std::unordered_map<std::string, std::shared_ptr<Connection>> g_connectionMap;
static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_connectionTimestamps;
static std::mutex g_connectionMutex;
static std::atomic<bool> g_running = true;

std::unique_ptr<UDPSocketAsync> udpSocket;

class PacketHandler : public INetworkIOEvents {
public:
    void OnRawDataReceived(const NetworkEndpoint& sender,
        const uint8_t* data,
        uint32_t size,
        OverlappedIOContext* context) override
    {
        std::string key = sender.ToString();
        std::shared_ptr<Connection> conn;

        {
            std::scoped_lock lock(g_connectionMutex);

            if (g_connectionMap.count(key) == 0) {
                auto newConn = std::make_shared<Connection>(sender);
                g_connectionMap[key] = newConn;
                g_connectionTimestamps[key] = std::chrono::steady_clock::now();

                RF_NETWORK_INFO("New connection created for {}", key);

                newConn->SetSendCallback([](const NetworkEndpoint& recipient, const std::vector<uint8_t>& packet) {
                    udpSocket->SendData(recipient, packet.data(), static_cast<uint32_t>(packet.size()));
                    });

                const auto& publicKey = newConn->GetLocalPublicKey();
                newConn->SendUnencrypted(std::vector<uint8_t>(publicKey.begin(), publicKey.end()));
            }

            conn = g_connectionMap[key];
        }

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

void SignalHandler(int) {
    g_running = false;
    RF_NETWORK_INFO("Shutdown signal received.");
}

void ReliabilityUpdateLoop() {
    uint64_t tickCounter = 0;

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        auto now = std::chrono::steady_clock::now();

        std::scoped_lock lock(g_connectionMutex);
        for (auto& [key, conn] : g_connectionMap) {
            if (!conn || !conn->IsConnected()) continue;

            auto& state = conn->GetReliableState();

            UDPReliabilityProtocol::ProcessRetransmissions(
                state, now,
                [conn](const std::vector<uint8_t>& pkt) {
                    conn->SendPacket(pkt);
                });

            if (UDPReliabilityProtocol::ShouldSendAck(state, now)) {
                constexpr uint8_t ACK_PACKET_TYPE = 6;
                auto ackPackets = UDPReliabilityProtocol::PrepareOutgoingPackets(
                    state, nullptr, 0, ACK_PACKET_TYPE, state.nextNonce++
                );

                for (auto& pkt : ackPackets) {
                    conn->SendPacket(pkt);
                }
            }

            if (++tickCounter % 50 == 0) {
                float rtt = state.smoothedRTT_ms;
                float rto = state.retransmissionTimeout_ms;
                RF_NETWORK_INFO("[{}] RTT: {:.2f} ms | RTO: {:.2f} ms | PendingAcks: {}",
                    key, rtt, rto, state.unacknowledgedSentPackets.size());
            }
        }
    }
}

int main() {
    Logger::Init();
    udpSocket = std::make_unique<UDPSocketAsync>();
    RF_NETWORK_INFO("=== RiftNet UDP Secure Server ===");

    std::signal(SIGINT, SignalHandler);

    PacketHandler handler;

    if (!udpSocket->Init("0.0.0.0", 7777, &handler)) {
        RF_NETWORK_ERROR("Failed to initialize UDPSocketAsync.");
        return 1;
    }

    if (!udpSocket->Start()) {
        RF_NETWORK_ERROR("Failed to start UDPSocketAsync.");
        return 1;
    }

    RF_NETWORK_INFO("Server listening on port 7777. Press Ctrl+C to stop.");

    std::thread reliabilityThread(ReliabilityUpdateLoop);

    while (g_running) {
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }

    reliabilityThread.join();
    udpSocket->Stop();

    RF_NETWORK_INFO("Server shut down cleanly.");
    return 0;
}
