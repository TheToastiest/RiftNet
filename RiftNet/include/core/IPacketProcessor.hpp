// include/RiftNet/IPacketProcessor.h
#pragma once

#include <cstdint>
#include <cstddef>

namespace RiftNet {
    class IPacketProcessor {
    public:
        virtual ~IPacketProcessor() = default;

        virtual bool ProcessIncomingPacket(const uint8_t* data, size_t length) = 0;
        // Could return true if valid + dispatched
    };
}
