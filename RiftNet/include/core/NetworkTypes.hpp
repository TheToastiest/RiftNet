#pragma once

#include <cstdint>
#include <cstddef>

namespace RiftNet
{
    struct PacketHeader
    {
        uint16_t packetId;
        uint16_t payloadSize;
    };

    constexpr size_t MAX_PACKET_SIZE = 1024;

    enum class PacketID : uint16_t
    {
        INVALID = 0,
        LOGIN_REQUEST = 1,
        PLAYER_MOVE = 2,
        HEARTBEAT = 3,

    };
}

namespace RiftForged {
    namespace Networking {

        using SequenceNumber = uint16_t;

    }
}

