// include/riftnet/RiftNetEngine.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>
#include <vector>
#include "RiftNetWire.hpp"

namespace RiftNet {

    using BroadcastFn = std::function<void(const uint8_t* bytes, size_t len)>;
    using SendOneFn = std::function<void(const char* key, const uint8_t* bytes, size_t len)>; // key = endpoint string

    struct IRiftNetEngine {
        virtual ~IRiftNetEngine() = default;
        virtual void Initialize(BroadcastFn bc, SendOneFn send_one) = 0;

        // Called each authoritative tick. If it returns true, fill sh+payload to broadcast.
        virtual bool Tick(uint64_t frame_idx,
            int64_t  t_pre_sim_qpc,       // for logging
            SnapshotHeader& sh,
            std::vector<uint8_t>& payload // engine fills
        ) = 0;

        // Called on client input (already decrypted & decompressed app bytes)
        virtual void OnInput(const InputPkt& in, const char* endpointKey) = 0;
    };

    // Factory you implement
    IRiftNetEngine* CreateRiftNetEngine();
    void DestroyRiftNetEngine(IRiftNetEngine*);

} // namespace RiftNet
