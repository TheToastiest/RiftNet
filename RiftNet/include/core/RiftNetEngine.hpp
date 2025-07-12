#pragma once

#include <memory>
#include <cstdint>

#include "IPacketProcessor.hpp"

namespace RiftNet
{
    class RiftNetEngine
    {
    public:
        explicit RiftNetEngine(std::shared_ptr<IPacketProcessor> processor);
        ~RiftNetEngine() = default;

        // Called by application or server loop
        void PollIncoming(); // Future-proof for socket reads

        // Future expansions:
        // void SendPacket(...);
        // void AddConnection(...);

    private:
        std::shared_ptr<IPacketProcessor> _processor;

        // When real socket layer is in:
        // std::unique_ptr<IUDPSocket> _socket;
    };
}
