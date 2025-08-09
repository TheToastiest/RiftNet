// bench_analyze.cpp (QPC-robust, C++17)
#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <unordered_map>
#include <algorithm>
#include <cstdint>
#include "nlohmann/json.hpp"
using json = nlohmann::json;

struct ServerFrame { int64_t t_pre_sim_ns{}, t_post_sim_ns{}, t_post_sim_qpc{}; };
struct Present { int frame{}; int64_t t_present_ns{}, t_present_qpc{}, offset_ns{}; };

static double pct(std::vector<int64_t> v, double p) {
    if (v.empty()) return 0.0;
    if (p < 0) p = 0; if (p > 1) p = 1;
    size_t k = (size_t)(p * (v.size() - 1));
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return (double)v[k];
}

int main(int argc, char** argv) {
    if (argc < 4) { std::cout << "Usage: bench_analyze <server_frames.jsonl> <client_frames.jsonl> <summary.json>\n"; return 1; }
    const std::string sp = argv[1], cp = argv[2], outp = argv[3];

    // Read server frames
    std::unordered_map<int, ServerFrame> server; server.reserve(1 << 16);
    {
        std::ifstream in(sp); std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto j = json::parse(line);
                if (!j.contains("frame")) continue;
                int f = j["frame"].get<int>();
                auto& sf = server[f];
                if (j.contains("t_pre_sim_ns"))   sf.t_pre_sim_ns = j["t_pre_sim_ns"].get<int64_t>();
                if (j.contains("t_post_sim_ns"))  sf.t_post_sim_ns = j["t_post_sim_ns"].get<int64_t>();
                if (j.contains("t_post_sim_qpc")) sf.t_post_sim_qpc = j["t_post_sim_qpc"].get<int64_t>();
            }
            catch (...) {}
        }
    }

    // Read client inputs & presents
    std::vector<int64_t> inputs; inputs.reserve(1 << 16);
    std::vector<Present> presents; presents.reserve(1 << 16);
    {
        std::ifstream in(cp); std::string line;
        while (std::getline(in, line)) {
            if (line.empty()) continue;
            try {
                auto j = json::parse(line);
                if (j.contains("t_input_ns")) { inputs.push_back(j["t_input_ns"].get<int64_t>()); continue; }
                if (j.contains("frame") && j.contains("t_present_ns")) {
                    Present p{};
                    p.frame = j["frame"].get<int>();
                    p.t_present_ns = j["t_present_ns"].get<int64_t>();
                    p.offset_ns = j.value("offset_ns", 0LL);
                    p.t_present_qpc = j.value("t_present_qpc", 0LL); // NEW
                    presents.push_back(p);
                }
            }
            catch (...) {}
        }
    }

    std::sort(inputs.begin(), inputs.end());
    std::sort(presents.begin(), presents.end(),
        [](const Present& a, const Present& b) { return a.t_present_ns < b.t_present_ns; });

    // Metric 1: Input -> Present (floor-join)
    std::vector<int64_t> e2e_ns; e2e_ns.reserve(std::min(inputs.size(), presents.size()));
    size_t i = 0, dropped = 0;
    for (const auto& pr : presents) {
        while ((i + 1) < inputs.size() && inputs[i + 1] <= pr.t_present_ns) ++i;
        if (i < inputs.size() && inputs[i] <= pr.t_present_ns)
            e2e_ns.push_back((pr.t_present_ns - inputs[i]) - pr.offset_ns);
        else
            ++dropped;
    }

    // Derive ns_per_tick from server frames (wide baseline)
    bool have_qpc = false; int64_t qpc_min = 0, qpc_max = 0, ns_min = 0, ns_max = 0;
    for (const auto& kv : server) {
        const auto& sf = kv.second;
        if (sf.t_post_sim_qpc && sf.t_post_sim_ns) {
            if (!have_qpc) { have_qpc = true; qpc_min = qpc_max = sf.t_post_sim_qpc; ns_min = ns_max = sf.t_post_sim_ns; }
            else {
                if (sf.t_post_sim_qpc < qpc_min) { qpc_min = sf.t_post_sim_qpc; ns_min = sf.t_post_sim_ns; }
                if (sf.t_post_sim_qpc > qpc_max) { qpc_max = sf.t_post_sim_qpc; ns_max = sf.t_post_sim_ns; }
            }
        }
    }
    double ns_per_tick = 0.0;
    if (have_qpc && qpc_max != qpc_min) ns_per_tick = double(ns_max - ns_min) / double(qpc_max - qpc_min);

    bool client_has_qpc = false;
    for (const auto& pr : presents) { if (pr.t_present_qpc) { client_has_qpc = true; break; } }

    // Metric 2: Server -> Present
    std::vector<int64_t> s2p_ns; s2p_ns.reserve(std::min(server.size(), presents.size()));
    std::string warning;
    if (have_qpc && client_has_qpc && ns_per_tick > 0.0) {
        for (const auto& pr : presents) {
            auto it = server.find(pr.frame);
            if (it == server.end()) continue;
            const auto& sf = it->second;
            if (!sf.t_post_sim_qpc) continue;
            double offset_ticks = double(pr.offset_ns) / ns_per_tick;
            double delta_ticks = (double)pr.t_present_qpc + offset_ticks - (double)sf.t_post_sim_qpc;
            s2p_ns.push_back((int64_t)(delta_ticks * ns_per_tick));
        }
    }
    else {
        // Fallback (ns-relative) — will include base mismatch => huge values
        warning = "QPC alignment unavailable (missing t_present_qpc or t_post_sim_qpc); server_to_present uses ns-relative fallback.";
        for (const auto& pr : presents) {
            auto it = server.find(pr.frame);
            if (it == server.end()) continue;
            const auto& sf = it->second;
            if (sf.t_post_sim_ns == 0) continue;
            s2p_ns.push_back((pr.t_present_ns - pr.offset_ns) - sf.t_post_sim_ns);
        }
    }

    // Server step
    std::vector<int64_t> step_ns; step_ns.reserve(server.size());
    for (const auto& kv : server) {
        const auto& sf = kv.second;
        if (sf.t_post_sim_ns >= sf.t_pre_sim_ns && sf.t_pre_sim_ns != 0)
            step_ns.push_back(sf.t_post_sim_ns - sf.t_pre_sim_ns);
    }

    json out;
    out["counts"] = {
        {"server_frames",(uint64_t)server.size()},
        {"client_inputs",(uint64_t)inputs.size()},
        {"client_presents",(uint64_t)presents.size()},
        {"dropped_presents",(uint64_t)dropped},
        {"e2e_samples",(uint64_t)e2e_ns.size()},
        {"server_present_matched",(uint64_t)s2p_ns.size()}
    };
    if (!e2e_ns.empty()) out["input_to_present_ms"] = { {"p50",pct(e2e_ns,0.50) / 1e6},{"p95",pct(e2e_ns,0.95) / 1e6},{"p99",pct(e2e_ns,0.99) / 1e6} };
    if (!s2p_ns.empty()) out["server_to_present_ms"] = { {"p50",pct(s2p_ns,0.50) / 1e6},{"p95",pct(s2p_ns,0.95) / 1e6},{"p99",pct(s2p_ns,0.99) / 1e6} };
    if (!step_ns.empty()) out["server_step_ms"] = { {"p50",pct(step_ns,0.50) / 1e6},{"p95",pct(step_ns,0.95) / 1e6},{"p99",pct(step_ns,0.99) / 1e6} };
    if (!warning.empty()) out["warnings"] = warning;
    if (have_qpc && ns_per_tick > 0.0) out["derived_qpc_ns_per_tick"] = ns_per_tick;

    std::ofstream o(outp); o << out.dump(2) << '\n';
    std::cout << "Wrote " << outp << "\n";
    return 0;
}
