#include "../../include/core/RiftNetEngine.hpp"
#include <iostream>

namespace RiftNet
{
    RiftNetEngine::RiftNetEngine(std::shared_ptr<IPacketProcessor> processor)
        : _processor(std::move(processor))
    {
        std::cout << "[RiftNetEngine] Initialized.\n";
    }

    void RiftNetEngine::PollIncoming()
    {
        // Placeholder: in a real server this would be triggered by socket read completion
        std::cout << "[RiftNetEngine] PollIncoming called (no-op).\n";

        // Later:
        // uint8_t buffer[MAX_PACKET_SIZE];
        // int received = _socket->Receive(buffer, MAX_PACKET_SIZE);
        // if (received > 0)
        //     _processor->ProcessIncomingPacket(buffer, received);
    }
}
