#pragma once

#include <cstdint>

namespace RiftForged {
    namespace Networking {

        enum class PacketType : uint8_t {
            Handshake = 0x00,
            ReliableAck = 0x01,
            PlayerAction = 0x02,
            ChatMessage = 0x03,
            GameState = 0x04,
            Heartbeat = 0x05,
            EchoTest = 0x06,
            // ... add more as needed
        };

    } // namespace Networking
} // namespace RiftForged
