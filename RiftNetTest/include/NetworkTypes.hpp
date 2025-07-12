#pragma once

#include <cstdint>
#include <cstddef>

namespace RiftNet
{
    // Packet structure: 2-byte ID + optional payload
    struct PacketHeader
    {
        uint16_t packetId;
        uint16_t payloadSize; // Optional: For consistency checks
    };

    // You can define global limits here
    constexpr size_t MAX_PACKET_SIZE = 1024;

    // Define your packet IDs here
    enum class PacketID : uint16_t
    {
        INVALID = 0,
        LOGIN_REQUEST = 1,
        PLAYER_MOVE = 2,
        HEARTBEAT = 3,
        // ...
    };
}

namespace RiftForged {
    namespace Networking {

        using SequenceNumber = uint16_t;

    }
}

