// Bench/Harness/Bench_Server/Adapter_RiftNet_Server.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mutex>
#include <unordered_map>
#include <vector>
#include <string>

#pragma comment(lib, "winmm.lib")

#include "Bench_Server_Shared.hpp"

// ---- RiftNet transport ----
#include "../../RiftNet/include/core/Logger.hpp"
#include "../../RiftNet/include/core/NetworkTypes.hpp"
#include "../../RiftNet/include/core/INetworkIOEvents.hpp"
#include "../../RiftNet/include/core/INetworkIO.hpp"
#include "../../RiftNet/include/core/NetworkEndpoint.hpp"
#include "../../RiftNet/include/platform/UDPSocketAsync.hpp"
#include "../../RiftNet/include/core/Connection.hpp"
#include "../../RiftNet/include/core/UDPReliabilityProtocol.hpp"
#include "../../RiftNet/include/core/RiftGlobals.hpp"

#include <spdlog/spdlog.h>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace RiftNet {
    enum class PacketType : uint8_t {
        EchoTest = 1,
        Input = 2, // C->S (optional)
        Snapshot = 3, // S->C
        TimeSync = 4  // S->C
    };

#pragma pack(push,1)
    struct MsgTimeSync {
        uint64_t frame_idx;
        int64_t  server_qpc_ticks;
    };
    struct SnapshotHeader {
        uint64_t frame_idx;
        uint32_t entity_count;
    };
#pragma pack(pop)
}

using namespace RiftForged::Networking;
using namespace RiftForged::Logging;

namespace RiftNetBenchAdapter {

    static std::atomic<bool>     g_run{ false };
    static HANDLE                g_thread = nullptr;
    static HANDLE                g_timer = nullptr;
    static IFrameHook* g_hook = nullptr;
    static ServerConfig          g_cfg{};
    static std::atomic<uint64_t> g_frame{ 0 };
    static LARGE_INTEGER         g_qpcFreq{};

    static std::unique_ptr<UDPSocketAsync> g_udp;
    static std::unordered_map<std::string, std::shared_ptr<Connection>> g_conns;
    static std::mutex             g_conns_mtx;

    static inline int64_t qpc_now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

    // MSVC-safe: no GNU '?:' shorthand
    static inline LONGLONG qpc_delta_to_rel100ns(int64_t delta_qpc) {
        if (delta_qpc <= 0) return -1; // fire ASAP
        LONGLONG r = -((delta_qpc * 10000000LL) / g_qpcFreq.QuadPart);
        if (r == 0) r = -1;            // never 0
        return r;
    }

    static void arm_timer_for_qpc_deadline(int64_t target_qpc) {
        LARGE_INTEGER due; due.QuadPart = qpc_delta_to_rel100ns(target_qpc - qpc_now());
        SetWaitableTimerEx(g_timer, &due, 0, nullptr, nullptr, nullptr, 0);
    }

    static void BroadcastReliable(uint8_t pktType, const uint8_t* body, size_t len) {
        std::scoped_lock lk(g_conns_mtx);
        for (auto& [_, c] : g_conns) {
            if (!c) continue;
            c->SendReliable(std::vector<uint8_t>(body, body + len), pktType);
        }
    }

    static void BroadcastTimeSyncWire(uint64_t frame_idx, int64_t server_qpc_ticks) {
        RiftNet::MsgTimeSync ts{ frame_idx, server_qpc_ticks };
        BroadcastReliable(static_cast<uint8_t>(RiftNet::PacketType::TimeSync),
            reinterpret_cast<const uint8_t*>(&ts), sizeof(ts));
    }

    static void BroadcastSnapshotWire(uint64_t frame_idx,
        const uint8_t* payload, size_t payload_len,
        uint32_t entity_count = 0)
    {
        RiftNet::SnapshotHeader sh{ frame_idx, entity_count };
        std::vector<uint8_t> buf(sizeof(sh) + payload_len);
        std::memcpy(buf.data(), &sh, sizeof(sh));
        if (payload_len) std::memcpy(buf.data() + sizeof(sh), payload, payload_len);

        BroadcastReliable(static_cast<uint8_t>(RiftNet::PacketType::Snapshot),
            buf.data(), buf.size());
    }

    class PacketHandler : public INetworkIOEvents {
    public:
        void OnRawDataReceived(const NetworkEndpoint& sender,
            const uint8_t* data,
            uint32_t size,
            OverlappedIOContext*) override
        {
            std::shared_ptr<Connection> conn;
            {
                std::scoped_lock lk(g_conns_mtx);
                auto key = sender.ToString();
                auto it = g_conns.find(key);
                if (it == g_conns.end()) {
                    auto nc = std::make_shared<Connection>(sender);
                    nc->SetSendCallback([](const NetworkEndpoint& to, const std::vector<uint8_t>& pkt) {
                        if (g_udp) g_udp->SendData(to, pkt.data(), static_cast<uint32_t>(pkt.size()));
                        });
                    nc->SetAppPacketCallback([](const std::string& /*key*/, uint8_t pktType,
                        const uint8_t* /*body*/, size_t /*len*/) {
                            // Optional: handle PacketType::Input here
                            (void)pktType;
                        });

                    const auto& pub = nc->GetLocalPublicKey();
                    nc->SendUnencrypted(std::vector<uint8_t>(pub.begin(), pub.end()));

                    it = g_conns.emplace(key, nc).first;
                    spdlog::info("[Adapter] New connection {}", key);
                }
                conn = it->second;
            }

            if (conn) conn->HandleRawPacket(std::vector<uint8_t>(data, data + size));
        }

        void OnSendCompleted(OverlappedIOContext*, bool, uint32_t) override {}
        void OnNetworkError(const std::string& msg, int code = 0) override {
            spdlog::error("[Adapter] Net error ({}): {}", code, msg);
        }
    };

    static DWORD WINAPI SimThread(LPVOID) {
        QueryPerformanceFrequency(&g_qpcFreq);
        timeBeginPeriod(1);

        g_timer = CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_ALL_ACCESS);
        if (!g_timer) g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (!g_timer) { timeEndPeriod(1); return 1; }

        const uint32_t tick_hz = g_cfg.tick_hz ? g_cfg.tick_hz : 120;
        const int64_t  tick_qpc = static_cast<int64_t>((double)g_qpcFreq.QuadPart / (double)tick_hz);
        const uint32_t timesync_every = g_cfg.timesync_every_frames ? g_cfg.timesync_every_frames : 30;

        int64_t next_deadline = qpc_now();
        std::vector<uint8_t> snap_payload; // optional state payload

        while (g_run.load(std::memory_order_relaxed)) {
            arm_timer_for_qpc_deadline(next_deadline);
            WaitForSingleObject(g_timer, INFINITE);
            if (!g_run.load(std::memory_order_relaxed)) break;

            const int64_t t0_qpc = qpc_now();
            const uint64_t frame = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;

            if (g_hook) g_hook->onFrameBegin(frame, t0_qpc);

            // TODO: sim step + g_hook->onAccumulate(...)

            const int64_t t1_qpc = qpc_now();
            if (g_hook) g_hook->onFrameEnd(frame, t1_qpc);

            BroadcastSnapshotWire(frame,
                snap_payload.empty() ? nullptr : snap_payload.data(),
                snap_payload.size(),
                /*entity_count*/ 0);

            if (timesync_every && (frame % timesync_every) == 0) {
                BroadcastTimeSyncWire(frame, t1_qpc);
            }

            next_deadline += tick_qpc;
            const int64_t now = qpc_now();
            if (now - next_deadline > 4 * tick_qpc) next_deadline = now + tick_qpc;
        }

        if (g_timer) { CancelWaitableTimer(g_timer); CloseHandle(g_timer); g_timer = nullptr; }
        timeEndPeriod(1);
        return 0;
    }

    bool StartServer(const ServerConfig& cfg, IFrameHook* hook) {
        if (g_run.load()) return true;

        Logger::Init();
        spdlog::info("=== Bench Adapter (RiftNet) ===");

        g_cfg = cfg;
        g_hook = hook;
        g_frame.store(0);

        g_udp = std::make_unique<UDPSocketAsync>();
        static PacketHandler handler;

        if (!g_udp->Init("0.0.0.0", cfg.port ? cfg.port : 4000, &handler)) {
            spdlog::error("UDPSocketAsync::Init failed");
            g_udp.reset();
            return false;
        }
        if (!g_udp->Start()) {
            spdlog::error("UDPSocketAsync::Start failed");
            g_udp.reset();
            return false;
        }

        g_run.store(true);
        g_thread = CreateThread(nullptr, 0, SimThread, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void StopServer() {
        if (!g_run.load()) return;
        g_run.store(false);

        if (g_timer) { CancelWaitableTimer(g_timer); }
        if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = nullptr; }

        {
            std::scoped_lock lk(g_conns_mtx);
            g_conns.clear();
        }
        if (g_udp) { g_udp->Stop(); g_udp.reset(); }

        spdlog::info("[Adapter] Stopped.");
    }

    void BroadcastTimeSync(const TimeSyncPacket& ts) {
        BroadcastTimeSyncWire(ts.frame_idx, static_cast<int64_t>(ts.server_qpc_ticks));
    }

    void RunLoopBlocking() {
        if (!g_thread) return;
        WaitForSingleObject(g_thread, INFINITE);
    }

} // namespace RiftNetBenchAdapter
