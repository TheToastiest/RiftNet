// File: HandshakePacket.hpp

#pragma once
#include <array>
#include <cstdint>

namespace RiftForged::Networking {

    struct HandshakePacket {
        static constexpr uint8_t OPCODE = 0x01;
        std::array<uint8_t, 32> publicKey;

        HandshakePacket() = default;

        HandshakePacket(const std::array<uint8_t, 32>& pubKey)
            : publicKey(pubKey) {
        }

        std::vector<uint8_t> Serialize() const {
            std::vector<uint8_t> data;
            data.push_back(OPCODE);
            data.insert(data.end(), publicKey.begin(), publicKey.end());
            return data;
        }

        static HandshakePacket Deserialize(const std::vector<uint8_t>& raw) {
            HandshakePacket pkt;
            if (raw.size() >= 33 && raw[0] == OPCODE) {
                std::copy(raw.begin() + 1, raw.begin() + 33, pkt.publicKey.begin());
            }
            return pkt;
        }
    };

} // namespace RiftForged::Networking