// Bench/Harness/Bench_Server/Adapter_RiftNet_Server.cpp
#include <unordered_map>
#include <memory>
#include <thread>
#include <atomic>
#include <chrono>
#include <mutex>
#include <csignal>
#include <vector>
#include <string>

#include <riftencrypt.hpp>
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "../include/core/Logger.hpp"
#include "../include/core/protocols.hpp"            // <- unified protocol/types
#include "../include/core/INetworkIOEvents.hpp"
#include "../include/core/INetworkIO.hpp"
#include "../include/core/NetworkEndpoint.hpp"
#include "../include/platform/UDPSocketAsync.hpp"
#include "../include/core/Connection.hpp"
#include "../include/core/UDPReliabilityProtocol.hpp"
#include "../include/core/RiftGlobals.hpp"
#include "Bench_Server_Shared.hpp"

using namespace RiftNet::Protocol;
using namespace RiftNet::Networking;
using namespace RiftNet::Logging;

namespace RiftNetBenchAdapter {

    // --- Globals ---
    static std::shared_ptr<RiftForged::Threading::TaskThreadPool> g_cryptoThreadPool;
    static std::unique_ptr<UDPSocketAsync> g_udpSocket;
    static std::unordered_map<std::string, std::shared_ptr<Connection>> g_connectionMap;
    static std::unordered_map<std::string, std::chrono::steady_clock::time_point> g_connectionTimestamps;
    static std::mutex g_connectionMutex;
    static std::atomic<bool> g_running{ false };
    static std::thread g_reliabilityThread;

    // --- Packet handler ---
    class PacketHandler : public INetworkIOEvents {
    public:
        void OnRawDataReceived(const NetworkEndpoint& sender,
            const uint8_t* data,
            uint32_t size,
            OverlappedIOContext* /*context*/) override
        {
            const std::string key = sender.ToString();
            std::shared_ptr<Connection> conn;

            {
                std::scoped_lock lock(g_connectionMutex);

                auto it = g_connectionMap.find(key);
                if (it == g_connectionMap.end()) {
                    auto newConn = std::make_shared<Connection>(sender, /*isServerRole=*/true);
                    g_connectionMap[key] = newConn;
                    g_connectionTimestamps[key] = std::chrono::steady_clock::now();

                    RF_NETWORK_INFO("New connection created for {}", key);

                    newConn->SetSendCallback([](const NetworkEndpoint& recipient,
                        const std::vector<uint8_t>& packet) {
                            if (g_udpSocket)
                                g_udpSocket->SendData(recipient, packet.data(),
                                    static_cast<uint32_t>(packet.size()));
                        });

                    // Kick off X25519 pubkey exchange (unencrypted 32 bytes)
                    const auto& publicKey = newConn->GetLocalPublicKey();
                    newConn->SendUnencrypted(std::vector<uint8_t>(publicKey.begin(), publicKey.end()));

                    it = g_connectionMap.find(key);
                }

                conn = it->second;
            }

            if (!conn) return;

            try {
                std::vector<uint8_t> payload(data, data + size);
                conn->HandleRawPacket(payload); // decrypt -> frame parse -> reliability -> decompress -> app
            }
            catch (const std::exception& ex) {
                RF_NETWORK_ERROR("[{}] OnRawDataReceived exception: {}", key, ex.what());
            }
            catch (...) {
                RF_NETWORK_ERROR("[{}] OnRawDataReceived unknown exception", key);
            }
        }

        void OnSendCompleted(OverlappedIOContext* context, bool success, uint32_t bytesSent) override
        {
            context->endpoint = NetworkEndpoint(context->remoteAddrNative);
            const auto targetStr = context->endpoint.ToString();

            if (success)
                RF_NETWORK_DEBUG("Send completed: {} bytes to {}", bytesSent, targetStr);
            else
                RF_NETWORK_ERROR("Send failed to {}", targetStr);
        }

        void OnNetworkError(const std::string& errorMessage, int errorCode = 0) override
        {
            RF_NETWORK_ERROR("Network error ({}): {}", errorCode, errorMessage);
        }
    };

    // --- Reliability loop ---
    static void ReliabilityUpdateLoop()
    {
        uint64_t tickCounter = 0;

        while (g_running.load(std::memory_order_relaxed)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            const auto now_tp = std::chrono::steady_clock::now();

            std::scoped_lock lock(g_connectionMutex);
            for (auto& [key, conn] : g_connectionMap) {
                if (!conn) continue;
                if (!conn->IsConnected()) continue;

                auto& state = conn->GetReliableState();

                // Retransmit full framed wires (UDPReliabilityProtocol holds them)
                UDPReliabilityProtocol::ProcessRetransmissions(
                    state,
                    now_tp,
                    /*sendFunc: framed -> encrypt+send*/
                    [conn](const std::vector<uint8_t>& framedWire) {
                        conn->SendFramed(framedWire);
                    });

                // Opportunistic ACK-only packet (empty payload)
                if (UDPReliabilityProtocol::ShouldSendAck(state, now_tp)) {
                    auto acks = UDPReliabilityProtocol::PrepareOutgoingPacketsFramed(
                        state,
                        PacketType::ReliableAck,
                        /*payload*/ nullptr,
                        /*size*/ 0,
                        /*nonce*/ state.nextNonce++   // mirrored in reliable header
                    );

                    for (auto& wire : acks) {
                        conn->SendFramed(wire);
                    }

                    {
                        std::scoped_lock stateLock(state.internalStateMutex);
                        state.hasPendingAckToSend = false;
                        state.lastPacketSentTime = now_tp;
                    }
                }

                // Telemetry every ~5s
                if (++tickCounter % 50 == 0) {
                    std::scoped_lock stateLock(state.internalStateMutex);
                    RF_NETWORK_INFO(
                        "[{}] RTT: {:.1f} ms | RTO: {:.1f} ms | PendingAcks: {} | InFlight: {}",
                        key,
                        state.smoothedRTT_ms,
                        state.retransmissionTimeout_ms,
                        state.hasPendingAckToSend ? 1 : 0,
                        static_cast<uint32_t>(state.unacknowledgedSentPackets.size())
                    );
                }
            }
        }
    }

    // --- API ---
    bool StartServer(const ServerConfig& cfg, IFrameHook* /*hook*/)
    {
        if (g_running.load()) return true;

        Logger::Init();
        RF_NETWORK_INFO("=== RiftNet Bench Adapter (Test Server Logic) ===");

        g_cryptoThreadPool = std::make_shared<RiftForged::Threading::TaskThreadPool>(12);
        g_udpSocket = std::make_unique<UDPSocketAsync>();
        static PacketHandler handler;

        uint16_t port = cfg.port ? cfg.port : 7777;
        if (!g_udpSocket->Init("0.0.0.0", port, &handler)) {
            RF_NETWORK_ERROR("Failed to initialize UDPSocketAsync.");
            g_udpSocket.reset();
            return false;
        }
        if (!g_udpSocket->Start()) {
            RF_NETWORK_ERROR("Failed to start UDPSocketAsync.");
            g_udpSocket.reset();
            return false;
        }

        g_running.store(true);
        g_reliabilityThread = std::thread(ReliabilityUpdateLoop);

        RF_NETWORK_INFO("Server listening on port {}.", port);
        return true;
    }

    void StopServer()
    {
        if (!g_running.load()) return;
        g_running.store(false);

        if (g_reliabilityThread.joinable())
            g_reliabilityThread.join();

        if (g_udpSocket) {
            g_udpSocket->Stop();
            g_udpSocket.reset();
        }

        {
            std::scoped_lock lock(g_connectionMutex);
            g_connectionMap.clear();
            g_connectionTimestamps.clear();
        }

        if (g_cryptoThreadPool) {
            g_cryptoThreadPool->stop();
            g_cryptoThreadPool.reset();
        }

        RF_NETWORK_INFO("Server shut down cleanly.");
    }

    void BroadcastTimeSync(const TimeSyncPacket& /*ts*/) { /* not used in bench */ }

    void RunLoopBlocking()
    {
        while (g_running.load())
            std::this_thread::sleep_for(std::chrono::seconds(1));
    }

} // namespace RiftNetBenchAdapter
