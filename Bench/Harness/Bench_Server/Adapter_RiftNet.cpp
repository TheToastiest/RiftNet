// Bench/Harness/Bench_Server/Adapter_RiftNet.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <mmsystem.h>
#pragma comment(lib, "winmm.lib")

#include "Bench_Server_Shared.hpp"

namespace RiftNetBenchAdapter {

    // -------- globals --------
    static std::atomic<bool>   g_run{ false };
    static HANDLE              g_thread = nullptr;
    static HANDLE              g_timer = nullptr;
    static IFrameHook* g_hook = nullptr;
    static ServerConfig        g_cfg{};
    static std::atomic<uint64_t> g_frame{ 0 };
    static LARGE_INTEGER       g_qpcFreq{};

    // -------- utility --------
    static inline int64_t qpc_now() {
        LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart;
    }

    // Convert a QPC delta (ticks) to relative 100ns units (negative for SetWaitableTimerEx)
    static inline LONGLONG qpc_delta_to_rel100ns(int64_t delta_qpc) {
        // (delta / freq) * 1e7, negated (relative time)
        if (delta_qpc <= 0) return -1; // fire ASAP
        // Use 64-bit math; delta_qpc is one tick period (few ms), so no overflow
        LONGLONG r = -((delta_qpc * 10000000LL) / g_qpcFreq.QuadPart);
        return (r == 0 ? -1 : r); // never 0
    }

    static void arm_timer_for_qpc_deadline(int64_t target_qpc) {
        const int64_t now = qpc_now();
        const int64_t delta = target_qpc - now;
        LARGE_INTEGER due; due.QuadPart = qpc_delta_to_rel100ns(delta);
        // one-shot, no period; high-res flag was chosen at creation
        SetWaitableTimerEx(g_timer, &due, 0, nullptr, nullptr, nullptr, 0);
    }

    // -------- sim thread --------
    static DWORD WINAPI SimThread(LPVOID) {
        QueryPerformanceFrequency(&g_qpcFreq);

        // High-resolution system timer (improves scheduler granularity)
        timeBeginPeriod(1);

        // Try to create a high-resolution waitable timer; fall back if unsupported
        DWORD flags = 0x00000002 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */;
        g_timer = CreateWaitableTimerExW(nullptr, nullptr, flags, TIMER_ALL_ACCESS);
        if (!g_timer) g_timer = CreateWaitableTimerExW(nullptr, nullptr, 0, TIMER_ALL_ACCESS);
        if (!g_timer) {
            // Fatal: no timer
            timeEndPeriod(1);
            return 1;
        }

        // Compute tick period in QPC ticks
        const int64_t tick_qpc = (int64_t)((double)g_qpcFreq.QuadPart / (double)(g_cfg.tick_hz ? g_cfg.tick_hz : 120));
        const uint32_t timesync_every = g_cfg.timesync_every_frames ? g_cfg.timesync_every_frames : 30;

        int64_t next_deadline = qpc_now(); // start immediately

        while (g_run.load(std::memory_order_relaxed)) {
            // Wait until deadline
            arm_timer_for_qpc_deadline(next_deadline);
            WaitForSingleObject(g_timer, INFINITE);

            if (!g_run.load(std::memory_order_relaxed)) break;

            const int64_t t0_qpc = qpc_now();
            const uint64_t frame = g_frame.fetch_add(1, std::memory_order_relaxed) + 1;

            if (g_hook) g_hook->onFrameBegin(frame, t0_qpc);

            // TODO: authoritative simulation step here
            // call g_hook->onAccumulate(EntityState{...}) for every finalized entity you want in the hash

            const int64_t t1_qpc = qpc_now();
            if (g_hook) g_hook->onFrameEnd(frame, t1_qpc);

            // periodic timesync piggybacked on frames
            if (timesync_every && (frame % timesync_every) == 0) {
                TimeSyncPacket pkt{};
                pkt.server_qpc_ticks = t1_qpc; // or qpc_now()
                pkt.frame_idx = frame;
                BroadcastTimeSync(pkt);
            }

            // schedule next
            next_deadline += tick_qpc;

            // catch-up if we fell far behind (avoid spiral of death)
            const int64_t now = qpc_now();
            if (now - next_deadline > 4 * tick_qpc) next_deadline = now + tick_qpc;
        }

        // cleanup
        if (g_timer) { CancelWaitableTimer(g_timer); CloseHandle(g_timer); g_timer = nullptr; }
        timeEndPeriod(1);
        return 0;
    }

    // -------- API --------
    bool StartServer(const ServerConfig& cfg, IFrameHook* hook) {
        if (g_run.load()) return true;
        g_cfg = cfg;
        g_hook = hook;
        g_frame.store(0);
        g_run.store(true);
        g_thread = CreateThread(nullptr, 0, SimThread, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void StopServer() {
        if (!g_run.load()) return;
        g_run.store(false);
        if (g_timer) { CancelWaitableTimer(g_timer); }
        if (g_thread) { WaitForSingleObject(g_thread, INFINITE); CloseHandle(g_thread); g_thread = nullptr; }
    }

    // Stub: replace with your actual broadcast to connected clients
    void BroadcastTimeSync(const TimeSyncPacket& pkt) {
        (void)pkt;
        // TODO: serialize & send pkt to all clients
        // pkt contains server_qpc_ticks + frame_idx
    }

    // If your app expects a blocking loop, just wait until StopServer() is called
    void RunLoopBlocking() {
        // sleep-wait until StopServer tears down the thread
        if (!g_thread) return;
        WaitForSingleObject(g_thread, INFINITE);
    }
}
