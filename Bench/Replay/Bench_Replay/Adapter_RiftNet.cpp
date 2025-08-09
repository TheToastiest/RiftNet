// Bench/Harness/Bench_Server/Adapter_RiftNet.cpp
#include <windows.h>
#include <vector>
#include <atomic>
#include "Bench_Server.cpp" // for IFrameHook, EntityState, TimeSyncPacket, ServerConfig
                    // (or move interfaces into a shared header)

namespace RiftNetBenchAdapter {

    // Replace with your globals/state
    static std::atomic<uint64_t> g_frame{ 0 };
    static IFrameHook* g_hook = nullptr;
    static ServerConfig g_cfg{};

    // Sim loop on a thread (demo). Replace with your real server tick.
    static DWORD WINAPI SimThread(LPVOID) {
        const double dt = 1.0 / (double)g_cfg.tick_hz;
        LARGE_INTEGER freq; QueryPerformanceFrequency(&freq);
        const int64_t target_ticks = (int64_t)(dt * (double)freq.QuadPart);

        while (true) {
            const uint64_t frame = g_frame.load() + 1;
            g_frame.store(frame);

            LARGE_INTEGER t0; QueryPerformanceCounter(&t0);
            if (g_hook) g_hook->onFrameBegin(frame, t0.QuadPart);

            // ---- Demo entity set; in real code, iterate your authoritative world.
            {
                EntityState e{};
                e.id = 1; e.px = (float)frame * 0.01f; e.py = 0.0f; e.pz = 0.0f; e.vx = 0.01f; e.vy = 0; e.vz = 0;
                if (g_hook) g_hook->onAccumulate(e);
            }

            LARGE_INTEGER t1; QueryPerformanceCounter(&t1);
            if (g_hook) g_hook->onFrameEnd(frame, t1.QuadPart);

            // Sleep to maintain tick (very rough)
            LARGE_INTEGER t2; QueryPerformanceCounter(&t2);
            int64_t elapsed = t2.QuadPart - t0.QuadPart;
            int64_t remain = target_ticks - elapsed;
            if (remain > 0) {
                DWORD ms = (DWORD)((remain * 1000ll) / freq.QuadPart);
                if (ms) Sleep(ms);
            }
        }
        return 0;
    }

    static HANDLE g_thread = nullptr;

    bool StartServer(const ServerConfig& cfg, IFrameHook* hook) {
        g_cfg = cfg;
        g_hook = hook;
        // TODO: init RiftNet server sockets, IOCP, channels, etc.
        // TODO: bind a packet-receive callback that calls into your input queue.

        g_thread = CreateThread(nullptr, 0, SimThread, nullptr, 0, nullptr);
        return g_thread != nullptr;
    }

    void StopServer() {
        // TODO: signal thread to stop, close sockets, free IOCP, etc.
        if (g_thread) {
            TerminateThread(g_thread, 0);  // bench only; replace with a clean stop
            CloseHandle(g_thread); g_thread = nullptr;
        }
    }

    void BroadcastTimeSync(const TimeSyncPacket& pkt) {
        // TODO: serialize & send over your chosen channel to all clients
        (void)pkt;
    }

    void RunLoopBlocking() {
        // If your server has a blocking loop, call it here instead of our SimThread.
        Sleep(INFINITE);
    }
}
