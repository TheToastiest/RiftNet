// src/RiftNetEngine.cpp
#include "../../include/core/RiftNetEngine.hpp"

#include <utility>   // for std::move

namespace RiftNet {

    class Engine final : public IRiftNetEngine {
        BroadcastFn bc_;
        SendOneFn   send_;

    public:
        void Initialize(BroadcastFn bc, SendOneFn s) override {
            bc_ = std::move(bc);
            send_ = std::move(s);
        }

        bool Tick(uint64_t frame,
            int64_t /*t_pre_sim_qpc*/,
            RiftNet::Protocol::Wire::SnapshotHeader& sh,
            std::vector<uint8_t>& payload) override
        {
            sh.frame_idx = frame;
            sh.entity_count = 0;     // no entities yet
            payload.clear();         // bench: empty body
            return true;             // send every tick for bench
        }

        void OnInput(const RiftNet::Protocol::Wire::InputPkt& in,
            const char* /*endpointKey*/) override
        {
            (void)in; // TODO: apply to sim
        }
    };

    IRiftNetEngine* CreateRiftNetEngine() { return new Engine(); }
    void DestroyRiftNetEngine(IRiftNetEngine* p) { delete p; }

} // namespace RiftNet
