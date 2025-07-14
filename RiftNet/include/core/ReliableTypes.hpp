#pragma once
#include <vector>
#include <cstdint>

namespace RiftForged::Networking {

    /*struct ReliablePacketHeader {
        uint16_t sequenceNumber;
        uint16_t ackNumber;
        uint32_t ackBitfield;
        uint8_t packetType;
        uint64_t nonce;
    };*/

   /* struct ReliablePacket {
        uint16_t sequenceNumber;
        uint8_t packetType;
        uint64_t nonce;
        std::vector<uint8_t> data;
        uint64_t timeSent;

        ReliablePacket() = default;

        ReliablePacket(uint16_t seq, uint8_t type, uint64_t nonceVal,
            const std::vector<uint8_t>& payload, uint64_t sentTime)
            : sequenceNumber(seq), packetType(type), nonce(nonceVal),
            data(payload), timeSent(sentTime) {
        }

        ReliablePacket(uint16_t seq, uint8_t type, uint64_t nonceVal,
            std::vector<uint8_t>&& payload, uint64_t sentTime)
            : sequenceNumber(seq), packetType(type), nonce(nonceVal),
            data(std::move(payload)), timeSent(sentTime) {
        }
    };*/

}
