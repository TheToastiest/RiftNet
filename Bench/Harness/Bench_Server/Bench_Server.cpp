// Bench/Harness/Bench_Server/Bench_Server.cpp
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <fstream>
#include <filesystem>
#include <iostream>

#include "Bench_Server_Shared.hpp" // QpcClock, EntityState, IFrameHook, TimeSyncPacket, ServerConfig, StartServer/StopServer/BroadcastTimeSync
using RiftNetBenchAdapter::BroadcastTimeSync;
// ---------------- QPC clock (safe) ----------------
QpcClock g_qpc;

// Relative converter so numbers are small & positive
struct QpcRel {
    const QpcClock* c{};
    int64_t base_ticks{};
    explicit QpcRel(const QpcClock* cc) : c(cc), base_ticks(cc->now_ticks()) {}
    inline int64_t to_ns_since_base(int64_t ticks) const {
        const int64_t dt = ticks - base_ticks;
        return c->to_ns_ticks(dt);
    }
};
static QpcRel g_rel(&g_qpc);

// --------------- Results path helpers ---------------
static std::string iso_now_utc() {
    SYSTEMTIME st; GetSystemTime(&st);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%04u-%02u-%02uT%02u-%02u-%02uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return buf;
}
static std::filesystem::path make_results_dir() {
    const auto run = iso_now_utc();
    const auto p = std::filesystem::path("Bench") / "Results" / run;
    std::filesystem::create_directories(p);
    return p;
}

// --------------- Frame hook implementation ---------------
class FrameHook final : public IFrameHook {
public:
    FrameHook(std::ofstream& log, uint64_t build_id, uint64_t seed, uint32_t timesync_every_frames)
        : log_(log), build_id_(build_id), seed_(seed), timesync_every_(timesync_every_frames ? timesync_every_frames : 30) {
    }

    void onFrameBegin(uint64_t frame_idx, int64_t t_pre_sim_qpc) override {
        frame_idx_ = frame_idx;
        t_pre_sim_qpc_ = t_pre_sim_qpc;
        HashBegin(frame_idx_, build_id_, seed_);
    }

    void onAccumulate(const EntityState& s) override {
        // canonical packed layout for hashing
        alignas(8) struct Pack { uint64_t id; float px, py, pz, vx, vy, vz; }
        p{ s.id, s.px, s.py, s.pz, s.vx, s.vy, s.vz };
        HashAccumulateEntity(s.id, &p, sizeof(p));
    }

    void onFrameEnd(uint64_t frame_idx, int64_t t_post_sim_qpc) override {
        uint64_t h[2]{ 0,0 };
        HashEnd(h);

        const int64_t pre_qpc = t_pre_sim_qpc_;
        const int64_t post_qpc = t_post_sim_qpc;

        const int64_t t_pre_ns = g_rel.to_ns_since_base(pre_qpc);
        const int64_t t_post_ns = g_rel.to_ns_since_base(post_qpc);

        char buf[320];
        std::snprintf(buf, sizeof(buf),
            "{\"frame\":%llu,"
            "\"t_pre_sim_qpc\":%lld,\"t_post_sim_qpc\":%lld,"
            "\"t_pre_sim_ns\":%lld,\"t_post_sim_ns\":%lld,"
            "\"hash_hi\":\"%016llx\",\"hash_lo\":\"%016llx\"}\n",
            (unsigned long long)frame_idx,
            (long long)pre_qpc, (long long)post_qpc,
            (long long)t_pre_ns, (long long)t_post_ns,
            (unsigned long long)h[0], (unsigned long long)h[1]);

        log_ << buf;
        if ((frame_idx & 0x3F) == 0) log_.flush();

        // TimeSync broadcast every N frames
        if ((frame_idx % timesync_every_) == 0) {
            TimeSyncPacket pkt{};
            pkt.server_qpc_ticks = g_qpc.now_ticks();
            pkt.frame_idx = frame_idx;
            BroadcastTimeSync(pkt);
        }
    }

private:
    std::ofstream& log_;
    uint64_t build_id_{};
    uint64_t seed_{};
    uint32_t timesync_every_{ 30 };
    uint64_t frame_idx_{};
    int64_t  t_pre_sim_qpc_{};
};

// --------------- usage/help ---------------
static void usage() {
    std::cout <<
        "Bench_Server\n"
        "  --port <u16>         (default 4000)\n"
        "  --tick <Hz>          (default 120)\n"
        "  --timesync-every <N> (default 30 frames)\n";
}

// --------------- main ---------------
int wmain(int argc, wchar_t** argv) {
    using namespace RiftNetBenchAdapter;

    ServerConfig cfg{}; // defaults come from Bench_Server_Shared.hpp
    for (int i = 1; i < argc; ++i) {
        std::wstring k = argv[i];
        auto next = [&](int i) { return (i + 1 < argc) ? argv[i + 1] : L""; };
        if (k == L"--port") { cfg.port = (uint16_t)std::stoi(next(i)); ++i; }
        else if (k == L"--tick") { cfg.tick_hz = (uint32_t)std::stoi(next(i)); ++i; }
        else if (k == L"--timesync-every") { cfg.timesync_every_frames = (uint32_t)std::stoi(next(i)); ++i; }
        else if (k == L"--help" || k == L"-h") { usage(); return 0; }
    }

    const auto results_dir = make_results_dir();
    const auto log_path = results_dir / "server_frames.jsonl";
    std::wcout << L"[Bench_Server] Writing: " << log_path.wstring() << L"\n";

    std::ofstream log(log_path, std::ios::binary);
    if (!log) {
        std::cerr << "Failed to open results file.\n";
        return 1;
    }

    FrameHook hook(log, cfg.build_id, cfg.rng_seed, cfg.timesync_every_frames);

    if (!StartServer(cfg, &hook)) {
        std::cerr << "StartServer failed.\n";
        return 2;
    }

    std::wcout << L"[Bench_Server] Running at " << cfg.tick_hz
        << L" Hz on port " << cfg.port
        << L" (timesync every " << cfg.timesync_every_frames << L" frames)\n";

    // If your server runs its own blocking loop, call it here.
    // Otherwise remove this and let your internal tick thread drive frames.
    RunLoopBlocking();

    StopServer();
    return 0;
}
