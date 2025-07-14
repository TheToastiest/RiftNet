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

        struct PlainPacketHeader {
            uint8_t packetType;      // 1 byte
            uint64_t nonce;          // 8 bytes
            uint32_t payloadSize;    // 4 bytes

            static constexpr size_t HeaderSize() { return 13; }

            std::vector<uint8_t> Serialize() const {
                std::vector<uint8_t> buffer(HeaderSize());
                buffer[0] = packetType;

                for (int i = 0; i < 8; ++i)
                    buffer[1 + i] = static_cast<uint8_t>((nonce >> (56 - i * 8)) & 0xFF);

                for (int i = 0; i < 4; ++i)
                    buffer[9 + i] = static_cast<uint8_t>((payloadSize >> (24 - i * 8)) & 0xFF);

                return buffer;
            }

            static bool Deserialize(const uint8_t* data, size_t length, PlainPacketHeader& out) {
                if (length < HeaderSize())
                    return false;

                out.packetType = data[0];

                out.nonce = 0;
                for (int i = 0; i < 8; ++i)
                    out.nonce |= static_cast<uint64_t>(data[1 + i]) << (56 - i * 8);

                out.payloadSize = 0;
                for (int i = 0; i < 4; ++i)
                    out.payloadSize |= static_cast<uint32_t>(data[9 + i]) << (24 - i * 8);

                return true;
            }
        };


    } // namespace Networking
} // namespace RiftForged
