#pragma once

#include "IPacketProcessor.hpp"
#include "NetworkTypes.hpp"

namespace RiftNet
{
    class PacketProcessor : public IPacketProcessor
    {
    public:
        PacketProcessor();
        virtual ~PacketProcessor() = default;

        bool ProcessIncomingPacket(const uint8_t* data, size_t length) override;

    private:
        // Optional callback hook or reference to event system
        // For now: just log or stub
    };
}
