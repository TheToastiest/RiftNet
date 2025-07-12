//#pragma once
//
//#include <cstdint>
//
//namespace RiftForged {
//    namespace Networking {
//
//        struct ReliablePacketHeader {
//            uint16_t sequenceNumber;     // This packet's sequence number
//            uint16_t ackNumber;          // Highest received sequence from peer
//            uint32_t ackBitfield;        // Bitfield of previous 32 received packets
//
//            uint8_t  packetType;         // Optional type field (e.g., 0x01 = PlayerAction, 0x02 = Heartbeat, etc.)
//            uint64_t nonce;              // Required for encryption/decryption with ChaCha20Poly1305
//
//            // Optional future expansion:
//            // uint8_t flags;           // e.g., compression, fragmentation, etc.
//            // uint64_t timestamp;      // Could be added later if needed
//
//            // You can also pack this struct tightly if space is critical
//        };
//
//        static constexpr size_t ReliablePacketHeaderSize = sizeof(ReliablePacketHeader);
//
//    } // namespace Networking
//} // namespace RiftForged
