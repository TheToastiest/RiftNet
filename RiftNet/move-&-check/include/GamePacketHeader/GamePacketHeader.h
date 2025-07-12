// File: GamePacketHeader.h
// RiftForged Game Engine
// Copyright (C) 2022-2028 RiftForged Team

#pragma once

#include <cstdint> // For fixed-width integer types like uint32_t, uint16_t

namespace RiftForged {
    namespace Networking {

        const uint32_t CURRENT_PROTOCOL_ID_VERSION = 0x00000005; // Version 0.0.5 with DragonflyDB -- OLD CLIENT WILL NO LONGER WORK

        using SequenceNumber = uint32_t;

        // --- Flags for GamePacketHeader::flags ---
        enum class GamePacketFlag : uint8_t {
            NONE = 0,
            IS_RELIABLE = 1 << 0,   // This packet requires acknowledgment and retransmission (0x1)
            IS_ACK_ONLY = 1 << 1,   // This packet contains only ACK information, no application payload (0x2)
            IS_HEARTBEAT = 1 << 2,  // This is a keep-alive packet
            IS_DISCONNECT = 1 << 3, // This packet signals a disconnection
            IS_FRAGMENT_START = 1 << 4,
            IS_FRAGMENT_END = 1 << 5,
        };

        // Helper functions for bitmask operations on GamePacketFlag.
        inline GamePacketFlag operator|(GamePacketFlag a, GamePacketFlag b) {
            return static_cast<GamePacketFlag>(static_cast<uint8_t>(a) | static_cast<uint8_t>(b));
        }
        inline uint8_t& operator|=(uint8_t& existingFlags, GamePacketFlag flagToAdd) {
            existingFlags |= static_cast<uint8_t>(flagToAdd);
            return existingFlags;
        }
        inline bool HasFlag(uint8_t headerFlags, GamePacketFlag flagToCheck) {
            if (flagToCheck == GamePacketFlag::NONE) return headerFlags == static_cast<uint8_t>(GamePacketFlag::NONE);
            return (headerFlags & static_cast<uint8_t>(flagToCheck)) == static_cast<uint8_t>(flagToCheck);
        }

#pragma pack(push, 1) // Ensure no padding is added by the compiler.

        struct GamePacketHeader {
            uint32_t protocolId;
            uint8_t flags;
            SequenceNumber sequenceNumber;
            SequenceNumber ackNumber;
            uint32_t ackBitfield;

            GamePacketHeader(uint8_t initialFlags = static_cast<uint8_t>(GamePacketFlag::NONE))
                : protocolId(CURRENT_PROTOCOL_ID_VERSION),
                flags(initialFlags),
                sequenceNumber(0),
                ackNumber(0),
                ackBitfield(0)
            {
            }
        };

#pragma pack(pop)

        constexpr size_t GetGamePacketHeaderSize() {
            return sizeof(GamePacketHeader);
        }

    } // namespace Networking
} // namespace RiftForged