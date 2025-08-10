// include/riftnet/RiftNetEngine.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include "protocols.hpp"

using namespace RiftNet::Protocol;

namespace RiftNet {

    using BroadcastFn = std::function<void(const uint8_t* bytes, std::size_t len)>;
    // key = endpoint string (e.g., "ip:port" or your session key)
    using SendOneFn = std::function<void(const char* key, const uint8_t* bytes, std::size_t len)>;

    struct IRiftNetEngine {
        virtual ~IRiftNetEngine() = default;

        // Network layer hands in functions to emit raw app bytes (already encrypted/compressed upstream).
        virtual void Initialize(BroadcastFn bc, SendOneFn send_one) = 0;

        // Called each authoritative tick.
        // If it returns true, fill 'sh' and 'payload' (payload is the variable-length body after SnapshotHeader).
        // The caller wraps these in the outer protocol header (PacketHeader with type=GameState).
        virtual bool Tick(uint64_t frame_idx,
            int64_t  t_pre_sim_qpc,                           // for logging/metrics
            RiftNet::Protocol::Wire::SnapshotHeader& sh,      // S->C snapshot header
            std::vector<uint8_t>& payload) = 0;               // S->C snapshot body

        // Called when a client input payload arrives (already decrypted & decompressed to app bytes).
        // Expecting the unified wire form.
        virtual void OnInput(const RiftNet::Protocol::Wire::InputPkt& in,
            const char* endpointKey) = 0;
    };

    // Factory you implement
    IRiftNetEngine* CreateRiftNetEngine();
    void DestroyRiftNetEngine(IRiftNetEngine*);

} // namespace RiftNet