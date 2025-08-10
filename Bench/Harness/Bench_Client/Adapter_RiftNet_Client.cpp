// Bench/Harness/Bench_Client/Adapter_RiftNet_Client.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <Ws2tcpip.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstring>

#include "../../RiftNet/include/core/Logger.hpp"
#include "../../RiftNet/include/core/protocols.hpp"            // <- unified protocol/types
#include "../../RiftNet/include/core/INetworkIOEvents.hpp"
#include "../../RiftNet/include/core/NetworkEndpoint.hpp"
#include "../../RiftNet/include/platform/UDPSocketAsync.hpp"

#include "../../RiftNet/include/core/KeyExchange.hpp"
#include "../../RiftNet/include/core/SecureChannel.hpp"
// REMOVE: #include "../../RiftNet/include/core/ReliableTypes.hpp"
#include "../../RiftNet/include/core/UDPReliabilityProtocol.hpp"

#include "RiftCompress.hpp"
#include "riftencrypt.hpp" // EnsureSodiumInit symbol (ok if no-op)

#include "Bench_Client_Shared.hpp"

using namespace RiftNet::Protocol;
using namespace RiftForged::Networking;
using namespace RiftForged::Logging;

namespace RiftNetBenchClient {

    // ---- callbacks wired by Bench_Client.cpp
    static OnSnapshotFn  g_onSnap = nullptr;
    static OnTimeSyncFn  g_onSync = nullptr;

    // ---- transport
    static std::unique_ptr<UDPSocketAsync> g_udp;
    static std::atomic<bool>               g_running{ false };
    static NetworkEndpoint                 g_serverEP;

    enum class WireState : uint8_t { Idle = 0, SentClientPub, SecureReady };

    struct PeerConnectionState {
        // crypto
        KeyExchange              ke;
        SecureChannel            secure;
        uint64_t                 txNonce{ 1 };      // encryption TX nonce (for datagrams)
        uint64_t                 lastRxNonce{ 0 };  // rolling RX window
        WireState                state{ WireState::Idle };

        // compression + reliability
        Compressor               comp{ std::make_unique<LZ4Algorithm>() };
        ReliableConnectionState  conn;
        UDPReliabilityProtocol   rel;

        // stats
        std::atomic<uint64_t>    bytesSent{ 0 };
        std::atomic<uint64_t>    pktsSent{ 0 };
        std::atomic<uint64_t>    iters{ 0 };
        float                    lastRTT{ 0.f };
        float                    lastRTO{ 0.f };

        std::mutex               mtx;
    };

    static std::unique_ptr<PeerConnectionState> g_peer;

    // ---------- helpers ----------
    static bool ResolveIPv4A(const char* host, uint16_t port, NetworkEndpoint& outEP) {
        sockaddr_in sin{}; sin.sin_family = AF_INET; sin.sin_port = htons(port);
        if (InetPtonA(AF_INET, host, &sin.sin_addr) == 1) { outEP = NetworkEndpoint(sin); return true; }
        char portbuf[16]; _snprintf_s(portbuf, _TRUNCATE, "%hu", port);
        ADDRINFOA hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP;
        ADDRINFOA* res = nullptr;
        if (GetAddrInfoA(host, portbuf, &hints, &res) != 0 || !res) return false;
        sin = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        FreeAddrInfoA(res);
        outEP = NetworkEndpoint(sin);
        return true;
    }

    static std::string Narrow(const wchar_t* ws) {
        if (!ws) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, ws, -1, nullptr, 0, nullptr, nullptr);
        std::string s; s.resize(n ? (n - 1) : 0);
        if (n > 1) WideCharToMultiByte(CP_UTF8, 0, ws, -1, s.data(), n, nullptr, nullptr);
        return s;
    }

    static void SendRaw(const NetworkEndpoint& to, const std::vector<uint8_t>& pkt) {
        if (!g_udp) return;
        g_udp->SendData(to, pkt.data(), static_cast<uint32_t>(pkt.size()));
    }

    static void SendEncrypted(PeerConnectionState& p, const std::vector<uint8_t>& wire) {
        auto enc = p.secure.Encrypt(wire, p.txNonce++); // datagram nonce
        if (!enc.empty()) {
            SendRaw(g_serverEP, enc);
            p.bytesSent += enc.size();
            p.pktsSent++;
        }
    }

    // LZ4 frame magic: 04 22 4D 18
    static inline bool looks_like_lz4_frame(const uint8_t* p, size_t n) {
        return n >= 4 && p[0] == 0x04 && p[1] == 0x22 && p[2] == 0x4D && p[3] == 0x18;
    }

    // ---------- IO adapter ----------
    class ClientIO : public INetworkIOEvents {
    public:
        void OnRawDataReceived(const NetworkEndpoint& sender,
            const uint8_t* data,
            uint32_t size,
            OverlappedIOContext*) override
        {
            (void)sender;
            auto* p = g_peer.get();
            if (!p || size == 0) return;

            std::lock_guard<std::mutex> lock(p->mtx);

            // --- handshake: expect 32B server pubkey ---
            if (p->state != WireState::SecureReady) {
                if (size == 32 && p->state == WireState::SentClientPub) {
                    KeyExchange::KeyBuffer serverKey{};
                    std::memcpy(serverKey.data(), data, 32);
                    p->ke.SetRemotePublicKey(serverKey);

                    KeyExchange::KeyBuffer rxKey{}, txKey{};
                    if (!p->ke.DeriveSharedKey(/*isServerRole=*/false, rxKey, txKey)) {
                        spdlog::error("[Client] Shared key failure.");
                        return;
                    }
                    p->secure.Initialize(rxKey, txKey);
                    p->state = WireState::SecureReady;
                    spdlog::info("[Client] Secure channel ready.");
                }
                return;
            }

            // --- decrypt (small rolling window) ---
            std::vector<uint8_t> cipher(data, data + size), plain;
            bool decrypted = false;
            for (uint64_t n = p->lastRxNonce + 1; n <= p->lastRxNonce + 5; ++n) {
                if (p->secure.Decrypt(cipher, plain, n)) { p->lastRxNonce = n; decrypted = true; break; }
            }
            if (!decrypted) { spdlog::warn("[Client] Decryption failed. size={} nextRxNonce={}", size, p->lastRxNonce + 1); return; }

            // --- framed reliability parse ---
            PacketType pid{};
            std::vector<uint8_t> body; // may be empty (ACK) or compressed (LZ4)
            if (!UDPReliabilityProtocol::ProcessIncomingWire(
                p->conn, plain.data(), static_cast<uint32_t>(plain.size()), pid, body))
            {
                // dup/out-of-window → protocol still updated ACK window
                return;
            }

            // ACKs carry no body; nothing to decompress/dispatch
            if (pid == PacketType::ReliableAck) {
                p->lastRTT = p->conn.smoothedRTT_ms;
                p->lastRTO = p->conn.retransmissionTimeout_ms;
                return;
            }

            // --- decompress only if it looks like an LZ4 frame ---
            std::vector<uint8_t> app;
            if (!body.empty() && looks_like_lz4_frame(body.data(), body.size())) {
                try {
                    app = p->comp.decompress(body);
                }
                catch (const std::exception& ex) {
                    spdlog::error("[Client] Decompression failed: {}", ex.what());
                    return;
                }
            }
            else {
                app = std::move(body); // treat as raw payload
            }

            // --- dispatch ---
            switch (pid) {
            case PacketType::GameState: // S->C snapshot
                if (g_onSnap && app.size() >= sizeof(RiftNet::WireSnapshotHeader)) {
                    RiftNet::WireSnapshotHeader sh{};
                    std::memcpy(&sh, app.data(), sizeof(sh));
                    const uint8_t* payload = app.data() + sizeof(sh);
                    const size_t   paylen = app.size() - sizeof(sh);
                    SnapshotHeader hdrOut{}; hdrOut.frame_idx = sh.frame_idx;
                    g_onSnap(hdrOut, payload, paylen);
                }
                break;
            case PacketType::ReliableAck:
            case PacketType::EchoTest:
                // optional
                break;
            default:
                break;
            }

            p->lastRTT = p->conn.smoothedRTT_ms;
            p->lastRTO = p->conn.retransmissionTimeout_ms;
        }

        void OnSendCompleted(OverlappedIOContext*, bool, uint32_t) override {}
        void OnNetworkError(const std::string& msg, int code = 0) override {
            (void)code; spdlog::error("[Client] Net error: {}", msg);
        }
    };

    static ClientIO g_io;

    // ---------- Public API ----------
    bool Connect(const ClientConfig& cfg) {
        if (g_running.load()) return true;

        Logger::Init();
        void EnsureSodiumInit();

        std::string host = Narrow(cfg.serverHost);
        if (host.empty()) host = "127.0.0.1";
        const uint16_t port = cfg.serverPort ? cfg.serverPort : 4000;

        if (!ResolveIPv4A(host.c_str(), port, g_serverEP)) {
            spdlog::error("[Client] ResolveIPv4 failed for {}:{}", host, port);
            return false;
        }

        g_udp = std::make_unique<UDPSocketAsync>();
        if (!g_udp->Init("0.0.0.0", 0, &g_io) || !g_udp->Start()) {
            spdlog::error("[Client] UDP init/start failed");
            g_udp.reset();
            return false;
        }

        g_peer = std::make_unique<PeerConnectionState>();

        // send our public key (32B) in the clear
        {
            auto& p = *g_peer;
            const auto& pub = p.ke.GetLocalPublicKey();
            SendRaw(g_serverEP, std::vector<uint8_t>(pub.begin(), pub.end()));
            p.state = WireState::SentClientPub;
            spdlog::info("[Client] Sent public key.");
        }

        g_running.store(true);
        spdlog::info("[Client] Connected (bound 0.0.0.0:0 → {}:{})", host, port);
        return true;
    }

    // retransmits + opportunistic ACKs + stats
    void Poll() {
        auto* p = g_peer.get();
        if (!p || p->state != WireState::SecureReady) return;

        std::lock_guard<std::mutex> lock(p->mtx);
        const auto now_tp = std::chrono::steady_clock::now();

        UDPReliabilityProtocol::ProcessRetransmissions(
            p->conn, now_tp,
            [&](const std::vector<uint8_t>& wire) { SendEncrypted(*p, wire); });

        if (UDPReliabilityProtocol::ShouldSendAck(p->conn, now_tp)) {
            auto ackWires = UDPReliabilityProtocol::PrepareOutgoingPacketsFramed(
                p->conn, PacketType::ReliableAck, nullptr, 0, /*reliable nonce*/ p->conn.nextNonce++);
            for (auto& w : ackWires) SendEncrypted(*p, w);

            std::lock_guard<std::mutex> sLock(p->conn.internalStateMutex);
            p->conn.hasPendingAckToSend = false;
            p->conn.lastPacketSentTime = now_tp;
        }

        if ((++p->iters % 50ull) == 0ull) {
            spdlog::info("[Client] RTT: {:.2f} ms | RTO: {:.2f} ms | Sent: {} pkts, {} bytes",
                p->conn.smoothedRTT_ms, p->conn.retransmissionTimeout_ms,
                (uint64_t)p->pktsSent, (uint64_t)p->bytesSent);
        }
    }

    // app input → compress → frame → encrypt+send
    void SendInput(const void* bytes, size_t len) {
        auto* p = g_peer.get();
        if (!p || p->state != WireState::SecureReady || !bytes || !len) return;

        std::lock_guard<std::mutex> lock(p->mtx);

        std::vector<uint8_t> body((const uint8_t*)bytes, (const uint8_t*)bytes + len);
        auto compressed = p->comp.compress(body);

        auto wires = UDPReliabilityProtocol::PrepareOutgoingPacketsFramed(
            p->conn, PacketType::PlayerAction,
            compressed.data(), static_cast<uint32_t>(compressed.size()),
            /*reliable nonce*/ p->conn.nextNonce++);

        for (auto& w : wires) SendEncrypted(*p, w);
    }

    void Disconnect() {
        g_running.store(false);
        g_peer.reset();
        if (g_udp) { g_udp->Stop(); g_udp.reset(); }
    }

    void SetOnSnapshot(OnSnapshotFn f) { g_onSnap = f; }
    void SetOnTimeSync(OnTimeSyncFn f) { g_onSync = f; }

} // namespace RiftNetBenchClient
