// Bench/Harness/Bench_Server/Bench_Server_Shared.hpp
#pragma once
#include <cstdint>
#include <cstddef>
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

// Minimal entity snapshot used for hashing
struct EntityState {
    uint64_t id;
    float px, py, pz;
    float vx, vy, vz;
};

struct IFrameHook {
    virtual void onFrameBegin(uint64_t frame_idx, int64_t t_pre_sim_qpc) = 0;
    virtual void onAccumulate(const EntityState& s) = 0;
    virtual void onFrameEnd(uint64_t frame_idx, int64_t t_post_sim_qpc) = 0;
    virtual ~IFrameHook() = default;
};

#pragma pack(push, 1)
struct TimeSyncPacket {
    uint32_t magic{ 0x53594E43 }; // 'C' 'N' 'Y' 'S' (whatever)
    uint16_t version{ 1 };
    uint16_t reserved{ 0 };
    uint64_t server_qpc_ticks{ 0 };
    uint64_t frame_idx{ 0 };
};
#pragma pack(pop)

namespace RiftNetBenchAdapter {
    struct ServerConfig {
        uint16_t port = 4000;
        uint32_t tick_hz = 120;
        uint64_t build_id = 0x00010000;
        uint64_t rng_seed = 0xC0FFEEULL;
        uint32_t timesync_every_frames = 30;
    };

    bool StartServer(const ServerConfig& cfg, IFrameHook* hook);
    void StopServer();
    void BroadcastTimeSync(const TimeSyncPacket& pkt);
    void RunLoopBlocking();
}



// StateHash API (C linkage)
#ifdef __cplusplus
extern "C" {
#endif
    void HashBegin(uint64_t frame_idx, uint64_t build_id, uint64_t seed);
    void HashAccumulateEntity(uint64_t entity_id, const void* bytes, size_t len);
    void HashEnd(uint64_t out_hash[2]);
#ifdef __cplusplus
}
#endif
