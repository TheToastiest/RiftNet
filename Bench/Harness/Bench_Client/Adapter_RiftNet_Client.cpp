// Bench/Harness/Bench_Client/Adapter_RiftNet_Client.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <mmsystem.h>
#include <atomic>
#include <cstdint>
#pragma comment(lib, "winmm.lib")

#include "Bench_Client_Shared.hpp" // expects: ClientConfig, SnapshotHeader, TimeSyncPacket
                                   // and typedefs: OnSnapshotFn, OnTimeSyncFn
                                   // and declarations we implement: Connect/Poll/SendInput/Disconnect/SetOnSnapshot/SetOnTimeSync
// --- at top of Adapter_RiftNet.cpp
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

static SOCKET g_udp = INVALID_SOCKET;
static sockaddr_in g_udpDst{};

static void UdpInit(uint16_t port = 4001) {
    WSADATA w; WSAStartup(MAKEWORD(2, 2), &w);
    g_udp = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    u_long nb = 1; ioctlsocket(g_udp, FIONBIO, &nb); // non-blocking ok
    g_udpDst.sin_family = AF_INET;
    g_udpDst.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    g_udpDst.sin_port = htons(port);
}
static void UdpSendFrame(uint64_t frame, int64_t post_qpc) {
    struct Msg { uint64_t frame; int64_t post_qpc; } m{ frame, post_qpc };
    sendto(g_udp, (const char*)&m, sizeof(m), 0, (sockaddr*)&g_udpDst, sizeof(g_udpDst));
}
static void UdpClose() { if (g_udp != INVALID_SOCKET) { closesocket(g_udp); g_udp = INVALID_SOCKET; WSACleanup(); } }

// --- in StartServer(...) after you launch the sim thread:
UdpInit(/*optional port*/);

// --- in StopServer():
UdpClose();

// --- inside your sim loop, right after onFrameEnd(frame, t1_qpc):
UdpSendFrame(frame, t1_qpc);
namespace RiftNetBenchClient {

    // ---- Globals -----------------------------------------------------------
    static OnSnapshotFn   g_onSnap = nullptr;
    static OnTimeSyncFn   g_onSync = nullptr;
    static std::atomic<bool> g_running{ false };
    static HANDLE         g_thread = nullptr;
    static HANDLE         g_timer = nullptr;
    static ClientConfig   g_cfg{};
    static std::atomic<uint64_t> g_frame{ 0 };
    static LARGE_INTEGER  g_qpcFreq{};

    // ---- Helpers -----------------------------------------------------------
    static inline int64_t qpc_now() { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

    // Convert QPC delta ticks to relative 100ns units (negative for SetWaitableTimerEx)
    static inline LONGLONG qpc_delta_to_rel100ns(int64_t delta_qpc) {
        if (delta_qpc <= 0) return -1; // fire ASAP
        LONGLONG rel = -((delta_qpc * 10000000LL) / g_qpcFreq.QuadPart);
        return (rel == 0 ? -1 : rel);
    }

    static void arm_timer_for_deadline_qpc(HANDLE hTimer, int64_t target_qpc) {
        const int64_t now = qpc_now();
        LARGE_INTEGER due;
        due.QuadPart = qpc_delta_to_rel100ns(target_qpc - now);
        SetWaitableTimerEx(hTimer, &due, 0, nullptr, nullptr, nullptr, 0);
    }

    // ---- Synthetic snapshot/timesync thread --------------------------------
    static DWORD WINAPI SynthThread(LPVOID) {
        QueryPerformanceFrequency(&g_qpcFreq);

        // Improve scheduler granularity (pair with timeEndPeriod in teardown)
        timeBeginPeriod(1);

        // High-resolution waitable timer (falls back if unsupported)
        DWORD flags = 0x2; // CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
        g_timer = CreateWaitableTimerExW(nullptr, nullptr, flags, TIMER_ALL_ACCESS);
        if (!g_timer) g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (!g_timer) { timeEndPeriod(1); return 1; }

        const uint32_t hz = (g_cfg.tick_hz ? g_cfg.tick_hz : 120);
        const int64_t  tick_qpc = (int64_t)((double)g_qpcFreq.QuadPart / (double)hz);
        const uint32_t timesync_every = 30; // emit a timesync every N frames

        int64_t next_deadline = qpc_now();

        while (g_running.load(std::memory_order_relaxed)) {
            // Wait until next synthetic snapshot deadline
            arm_timer_for_deadline_qpc(g_timer, next_deadline);
            if (WaitForSingleObject(g_timer, INFINITE) != WAIT_OBJECT_0) break;
            if (!g_running.load(std::memory_order_relaxed)) break;

            // Synthesize "snapshot arrived" for frame f
            const uint64_t f = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;
            if (g_onSnap) {
                SnapshotHeader hdr{ f };
                g_onSnap(hdr, nullptr, 0);
            }

            // Periodic timesync to drive clock offset filter on client
            if (g_onSync && (f % timesync_every) == 0) {
                TimeSyncPacket ts{};
                ts.version = 1;
                ts.magic = 0x53594E43;        // 'SYNC'
                ts.server_qpc_ticks = (uint64_t)qpc_now(); // pretend this is the server's QPC
                ts.frame_idx = f;
                g_onSync(ts);
            }

            // Schedule next tick; catch up if we slipped far behind
            next_deadline += tick_qpc;
            const int64_t now = qpc_now();
            if (now - next_deadline > 4 * tick_qpc) next_deadline = now + tick_qpc;
        }

        // Teardown
        CancelWaitableTimer(g_timer);
        CloseHandle(g_timer); g_timer = nullptr;
        timeEndPeriod(1);
        return 0;
    }

    // ---- API: called by your bench app -------------------------------------
    bool Connect(const ClientConfig& cfg) {
        if (g_running.load()) return true;
        g_cfg = cfg;
        g_frame.store(0);
        g_running.store(true);

        // Start synthetic driver thread so your OnSnapshot logger fires
        g_thread = CreateThread(nullptr, 0, SynthThread, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void Poll() {
        // No-op for synthetic mode.
        // When real networking is wired, pump your recv queue here and call:
        //   if (g_onSnap) g_onSnap(hdr, payload_ptr, payload_len);
        //   if (g_onSync) g_onSync(ts);
    }

    void SendInput(const void* bytes, size_t len) {
        (void)bytes; (void)len;
        // When real networking is wired, serialize & send inputs here.
    }

    void Disconnect() {
        if (!g_running.load()) return;
        g_running.store(false);
        if (g_timer) CancelWaitableTimer(g_timer);
        if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = nullptr; }
    }

    void SetOnSnapshot(OnSnapshotFn f) { g_onSnap = f; }
    void SetOnTimeSync(OnTimeSyncFn f) { g_onSync = f; }
}
