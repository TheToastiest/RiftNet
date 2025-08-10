#pragma once
#include <cstdint>
#include <windows.h>

inline int64_t qpc_to_ns_safe(int64_t ticks, int64_t freq) {
    const int64_t sec = ticks / freq;        // whole seconds
    const int64_t rem = ticks % freq;        // remainder ticks, 0 <= rem < freq
    // sec * 1e9 fits in int64 for ~292 years; (rem * 1e9)/freq fits because rem < freq
    return sec * 1000000000LL + (rem * 1000000000LL) / freq;
}
struct QpcClock {
    LARGE_INTEGER f{};
    QpcClock() { QueryPerformanceFrequency(&f); }
    inline int64_t now_ticks() const { LARGE_INTEGER t; QueryPerformanceCounter(&t); return t.QuadPart; }

    // SAFE ticks→ns (no overflow)
    inline int64_t to_ns_ticks(int64_t ticks) const {
        const int64_t sec = ticks / f.QuadPart;                 // whole seconds
        const int64_t rem = ticks % f.QuadPart;                 // remainder ticks
        return sec * 1000000000LL + (rem * 1000000000LL) / f.QuadPart;
    }

    // Convenience: absolute QPC→ns since boot
    inline int64_t to_ns(int64_t abs_ticks) const { return to_ns_ticks(abs_ticks); }
};

extern QpcClock g_qpc;

// must match server
#pragma pack(push, 1)
struct TimeSyncPacket {
    uint32_t magic{ 0x53594E43 };
    uint16_t version{ 1 };
    uint16_t reserved{ 0 };
    uint64_t server_qpc_ticks{ 0 };
    uint64_t frame_idx{ 0 };
};
#pragma pack(pop)

// minimal snapshot shape for timestamp hook (adjust to your real snapshot)
struct SnapshotHeader {
    uint64_t frame_idx;
    uint32_t entity_count;
};
namespace RiftNet {
#pragma pack(push,1)
    struct MsgTimeSync {
        uint64_t frame_idx;
        int64_t  server_qpc_ticks;
    };
    struct WireSnapshotHeader {
        uint64_t frame_idx;
        uint32_t entity_count;
    };
#pragma pack(pop)
}
// Adapter you bind to RiftNet client
namespace RiftNetBenchClient {
    struct ClientConfig {
        wchar_t serverHost[256]{ L"127.0.0.1" };
        uint16_t serverPort{ 4000 };
        uint32_t tick_hz{ 120 };
        uint32_t input_hz{ 120 };
        uint32_t duration_sec{ 30 };
    };

    // Connect, start networking threads, subscribe to snapshots & timesync
    bool Connect(const ClientConfig& cfg);

    // Non-blocking pump; deliver callbacks below
    void Poll();

    // Sends one input packet (you implement actual payload)
    void SendInput(const void* bytes, size_t len);

    // Shutdown
    void Disconnect();

    // Register callbacks
    using OnSnapshotFn = void(*)(const SnapshotHeader&, const void* payload, size_t len);
    using OnTimeSyncFn = void(*)(const TimeSyncPacket&);
    void SetOnSnapshot(OnSnapshotFn);
    void SetOnTimeSync(OnTimeSyncFn);


}
// Safe QPC→ns conversion (no 64-bit overflow)

