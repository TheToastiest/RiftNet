#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <string>
#include <iostream>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <chrono>
#include "Bench_Client_Shared.hpp"
#include "core/protocols.hpp"
using namespace RiftNet::Protocol::Wire;
QpcClock g_qpc;

struct QpcRel {
    const QpcClock* c; int64_t base_ticks;
    explicit QpcRel(const QpcClock* cc) : c(cc), base_ticks(cc->now_ticks()) {}
    inline int64_t to_ns_since_base(int64_t ticks) const {
        const int64_t dt = ticks - base_ticks;
        return c->to_ns_ticks(dt);
    }
};
static QpcRel g_rel(&g_qpc);


// simple json escape
static std::string esc(const std::string& s) {
    std::string o; o.reserve(s.size() + 8);
    for (char c : s) {
        switch (c) {
        case '\\': o += "\\\\"; break; case '"': o += "\\\""; break;
        case '\n': o += "\\n"; break; case '\r': o += "\\r"; break; case '\t': o += "\\t"; break;
        default:o += c;
        }
    }
    return o;
}

static std::wstring warg(int i, int argc, wchar_t** argv) { return (i < argc) ? std::wstring(argv[i]) : L""; }

static std::string iso_now_utc() {
    SYSTEMTIME st; GetSystemTime(&st);
    char b[64];
    std::snprintf(b, sizeof(b), "%04u-%02u-%02uT%02u-%02u-%02uZ",
        st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
    return b;
}

static std::filesystem::path make_results_dir() {
    auto p = std::filesystem::path("Bench") / "Results" / iso_now_utc();
    std::filesystem::create_directories(p);
    return p;
}

// clock sync (offset = server_qpc - client_qpc)
struct ClockSync {
    double alpha{ 0.1 };
    double offset_ticks{ 0 };
    void on_timesync(uint64_t server_ticks) {
        int64_t c = g_qpc.now_ticks();
        double est = (double)server_ticks - (double)c;
        offset_ticks = (1.0 - alpha) * offset_ticks + alpha * est;
    }
    int64_t offset_ns() const { return g_qpc.to_ns((int64_t)offset_ticks); }
} g_sync;

static std::ofstream g_log;
static std::filesystem::path g_log_path;

// minimal snapshot callback → capture “present”
static void OnSnapshot(const SnapshotHeader& hdr, const void*, size_t) {
    const int64_t present_qpc = g_qpc.now_ticks();                 // NEW
    const int64_t present_ns = g_rel.to_ns_since_base(present_qpc);
    const int64_t offs_ns = g_sync.offset_ns(); // server_qpc - client_qpc (ns)
    char buf[320];
    std::snprintf(buf, sizeof(buf),
        "{\"frame\":%llu,\"t_present_qpc\":%lld,\"t_present_ns\":%lld,\"offset_ns\":%lld}\n",
        (unsigned long long)hdr.frame_idx,
        (long long)present_qpc, (long long)present_ns, (long long)offs_ns);
    g_log << buf;
}


// timesync callback
static void OnTimeSync(const TimeSyncPacket& ts) {
    if (ts.version != 1 || ts.magic != 0x53594E43) return;
    g_sync.on_timesync(ts.server_qpc_ticks);
}

static void usage() {
    std::cout <<
        "Bench_Client\n"
        "  --host <name|ip> (default 127.0.0.1)\n"
        "  --port <u16>     (default 4000)\n"
        "  --tick <Hz>      (default 120)\n"
        "  --input <Hz>     (default 120)\n"
        "  --secs <n>       (default 30)\n";
}

int wmain(int argc, wchar_t** argv) {
    RiftNetBenchClient::ClientConfig cfg{};
    wcscpy_s(cfg.serverHost, L"127.0.0.1");
    for (int i = 1; i < argc; i++) {
        auto k = warg(i, argc, argv);
        if (k == L"--host" && i + 1 < argc) { wcscpy_s(cfg.serverHost, warg(++i, argc, argv).c_str()); }
        else if (k == L"--port" && i + 1 < argc) { cfg.serverPort = (uint16_t)std::stoi(warg(++i, argc, argv)); }
        else if (k == L"--tick" && i + 1 < argc) { cfg.tick_hz = (uint32_t)std::stoi(warg(++i, argc, argv)); }
        else if (k == L"--input" && i + 1 < argc) { cfg.input_hz = (uint32_t)std::stoi(warg(++i, argc, argv)); }
        else if (k == L"--secs" && i + 1 < argc) { cfg.duration_sec = (uint32_t)std::stoi(warg(++i, argc, argv)); }
        else if (k == L"--help" || k == L"-h") { usage(); return 0; }
    }

    auto dir = make_results_dir();
    g_log_path = dir / "client_frames.jsonl";
    g_log.open(g_log_path, std::ios::binary);
    if (!g_log) { std::cerr << "log open fail\n"; return 2; }

    RiftNetBenchClient::SetOnSnapshot(&OnSnapshot);
    RiftNetBenchClient::SetOnTimeSync(&OnTimeSync);
    if (!RiftNetBenchClient::Connect(cfg)) {
        std::cerr << "connect fail\n"; return 3;
    }

    std::wcout << L"[Bench_Client] Connected to " << cfg.serverHost << L":" << cfg.serverPort << L"\n";
    std::wcout << L"[Bench_Client] Logging " << std::wstring(g_log_path.wstring()) << L"\n";

    LARGE_INTEGER fq; QueryPerformanceFrequency(&fq);
    uint32_t safe_hz = (cfg.input_hz > 0) ? cfg.input_hz : 1;
    const int64_t input_ticks = (int64_t)((1.0 / (double)safe_hz) * (double)fq.QuadPart);
    int64_t next_input = g_qpc.now_ticks();
    const int64_t end_time = next_input + (int64_t)((double)cfg.duration_sec * (double)fq.QuadPart);

    struct InputPkt { uint64_t monotonic; float ax, ay; } in{ 0,0,0 };

    while (g_qpc.now_ticks() < end_time) {
        RiftNetBenchClient::Poll();

        int64_t now = g_qpc.now_ticks();
        if (now >= next_input) {
            const int64_t t_input_ns = g_rel.to_ns_since_base(now);
            char line[256];
            std::snprintf(line, sizeof(line),
                "{\"input_monotonic\":%llu,\"t_input_ns\":%lld}\n",
                (unsigned long long)in.monotonic, (long long)t_input_ns);
            g_log << line;

            in.monotonic++;
            in.ax += 0.01f; in.ay += 0.02f;
            RiftNetBenchClient::SendInput(&in, sizeof(in));

            next_input += input_ticks;
        }
        Sleep(0);
    }

    RiftNetBenchClient::Disconnect();
    g_log.flush();
    std::wcout << L"[Bench_Client] Done.\n";
    return 0;
}
