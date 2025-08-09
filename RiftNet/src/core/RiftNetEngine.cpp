// src/RiftNetEngine.cpp
#include "../../include/core/RiftNetEngine.hpp"
namespace RiftNet {

    class Engine final : public IRiftNetEngine {
        BroadcastFn bc_; SendOneFn send_;
    public:
        void Initialize(BroadcastFn bc, SendOneFn s) override { bc_ = std::move(bc); send_ = std::move(s); }
        bool Tick(uint64_t frame, int64_t /*t0*/, SnapshotHeader& sh, std::vector<uint8_t>& payload) override {
            sh.frame_idx = frame;
            sh.entity_count = 0;
            payload.clear();     // no entities yet
            return true;         // send every tick for bench
        }
        void OnInput(const InputPkt& in, const char* /*key*/) override {
            (void)in; // TODO: apply to sim
        }
    };

    IRiftNetEngine* CreateRiftNetEngine() { return new Engine(); }
    void DestroyRiftNetEngine(IRiftNetEngine* p) { delete p; }

} // namespace RiftNet
