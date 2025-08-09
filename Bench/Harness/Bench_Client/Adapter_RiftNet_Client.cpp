// Bench/Harness/Bench_Client/Adapter_RiftNet_Client.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <mutex>
#include <string>
#include <vector>
#include <memory>

#include "../../RiftNet/include/core/Logger.hpp"
#include "../../RiftNet/include/core/NetworkTypes.hpp"
#include "../../RiftNet/include/core/INetworkIOEvents.hpp"
#include "../../RiftNet/include/core/NetworkEndpoint.hpp"
#include "../../RiftNet/include/platform/UDPSocketAsync.hpp"
#include "../../RiftNet/include/core/Connection.hpp"
#include "../../RiftNet/include/core/RiftGlobals.hpp"

#include "Bench_Client_Shared.hpp"

using namespace RiftForged::Networking;
using namespace RiftForged::Logging;
namespace RiftNet {

    // must match server
    enum class PacketType : uint8_t {
        EchoTest = 1,
        Input = 2, // C->S
        Snapshot = 3, // S->C
        TimeSync = 4  // S->C
    };

#pragma pack(push,1)
    struct MsgTimeSync {
        uint64_t frame_idx;
        int64_t  server_qpc_ticks;
    };

#pragma pack(pop)

} // namespace RiftNet

namespace RiftNetBenchClient {

    // ---- callbacks wired by Bench_Client.cpp
    static OnSnapshotFn  g_onSnap = nullptr;
    static OnTimeSyncFn  g_onSync = nullptr;

    // ---- transport
    static std::unique_ptr<UDPSocketAsync> g_udp;
    static std::shared_ptr<Connection>     g_conn;
    static std::atomic<bool>               g_running{ false };
    static NetworkEndpoint                 g_serverEP;

    // simple resolver -> IPv4
    static bool ResolveIPv4(const wchar_t* host, uint16_t port, NetworkEndpoint& outEP) {
        wchar_t portw[16];
        _snwprintf_s(portw, _TRUNCATE, L"%hu", port);

        ADDRINFOW hints{}; hints.ai_family = AF_INET; hints.ai_socktype = SOCK_DGRAM; hints.ai_protocol = IPPROTO_UDP;
        ADDRINFOW* res = nullptr;
        if (GetAddrInfoW(host, portw, &hints, &res) != 0 || !res) return false;
        sockaddr_in sin = *reinterpret_cast<sockaddr_in*>(res->ai_addr);
        FreeAddrInfoW(res);
        outEP = NetworkEndpoint(sin);
        return true;
    }

    class ClientIO : public INetworkIOEvents {
    public:
        void OnRawDataReceived(const NetworkEndpoint& sender,
            const uint8_t* data,
            uint32_t size,
            OverlappedIOContext*) override
        {
            (void)sender;
            auto c = g_conn;
            if (!c) return;
            c->HandleRawPacket(std::vector<uint8_t>(data, data + size));
        }
        void OnSendCompleted(OverlappedIOContext*, bool, uint32_t) override {}
        void OnNetworkError(const std::string& msg, int code = 0) override {
            (void)code; (void)msg; // optional: log
        }
    };

    static ClientIO g_io;

    bool Connect(const ClientConfig& cfg) {
        if (g_running.load()) return true;

        if (!ResolveIPv4(cfg.serverHost, cfg.serverPort ? cfg.serverPort : 4000, g_serverEP))
            return false;

        Logger::Init(); // no-op if already inited

        g_udp = std::make_unique<UDPSocketAsync>();
        g_udp->Init("0.0.0.0", 0 /*ephemeral*/, &g_io);
        if (!g_udp->Start()) return false;

        // single connection to server endpoint
        g_conn = std::make_shared<Connection>(g_serverEP);

        // send path -> UDP
        g_conn->SetSendCallback([](const NetworkEndpoint& to, const std::vector<uint8_t>& pkt) {
            if (g_udp) g_udp->SendData(to, pkt.data(), static_cast<uint32_t>(pkt.size()));
            });

        // app dispatch -> fire bench callbacks
        g_conn->SetAppPacketCallback([](const std::string& /*key*/, uint8_t pktType,
            const uint8_t* body, size_t len)
            {
                switch (static_cast<RiftNet::PacketType>(pktType)) {
                case RiftNet::PacketType::TimeSync:
                    if (len >= sizeof(RiftNet::MsgTimeSync) && g_onSync) {
                        RiftNet::MsgTimeSync ts{};
                        std::memcpy(&ts, body, sizeof(ts));

                        TimeSyncPacket pkt{};
                        pkt.version = 1;
                        pkt.magic = 0x53594E43;
                        pkt.frame_idx = ts.frame_idx;
                        pkt.server_qpc_ticks = ts.server_qpc_ticks;
                        g_onSync(pkt);
                    }
                    break;
                case RiftNet::PacketType::Snapshot:
                    if (len >= sizeof(SnapshotHeader) && g_onSnap) {
                        SnapshotHeader sh{};
                        std::memcpy(&sh, body, sizeof(sh));
                        const uint8_t* payload = body + sizeof(sh);
                        const size_t   paylen = len - sizeof(sh);

                        SnapshotHeader hdr{};
                        hdr.frame_idx = sh.frame_idx;
                        hdr.entity_count = sh.entity_count;
                        g_onSnap(hdr, payload, paylen);
                    }
                    break;
                default:
                    break;
                }
            });

        // kick handshake by sending our pubkey first
        {
            const auto& pub = g_conn->GetLocalPublicKey();
            g_conn->SendUnencrypted(std::vector<uint8_t>(pub.begin(), pub.end()));
        }

        g_running.store(true);
        return true;
    }

    void Poll() {
        // IOCP is threaded in UDPSocketAsync; nothing to do here.
        // keep for API symmetry / future single-threaded fallback.
    }

    void SendInput(const void* bytes, size_t len) {
        auto c = g_conn;
        if (!c || !bytes || !len) return;
        // Your app-level input blob goes as the reliable body with PacketType::Input
        c->SendReliable(std::vector<uint8_t>((const uint8_t*)bytes, (const uint8_t*)bytes + len),
            static_cast<uint8_t>(RiftNet::PacketType::Input));
    }

    void Disconnect() {
        g_running.store(false);
        g_conn.reset();
        if (g_udp) { g_udp->Stop(); g_udp.reset(); }
    }

    void SetOnSnapshot(OnSnapshotFn f) { g_onSnap = f; }
    void SetOnTimeSync(OnTimeSyncFn f) { g_onSync = f; }

} // namespace RiftNetBenchClient
