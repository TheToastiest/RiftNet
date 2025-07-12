#pragma once

#include <cstdint>

namespace RiftForged {
    namespace Networking {

        struct ReliablePacketHeader {
            uint16_t sequenceNumber;    // This packet's sequence
            uint16_t ackNumber;         // Highest received from peer
            uint32_t ackBitfield;       // Bitfield of previous 32 packets received

            // Optional flags can be added later
            // uint8_t flags;
        };

    } // namespace Networking
} // namespace RiftForged