#pragma once

#include <cstdint>
#include <cstddef>
#include <utility>
#include <vector>
#include <list>
#include <chrono>
#include <mutex>

namespace RiftNet::Protocol {

    // =========================
    // Protocol constants
    // =========================
    constexpr uint32_t PROTOCOL_MAGIC = 0x52494654u; // 'RIFT' (wire: 0x52 0x49 0x46 0x54)
    constexpr uint16_t PROTOCOL_VERSION = 0x0001;

    constexpr std::size_t MAX_PACKET_SIZE = 1024;

    // Header is: 4(magic) + 2(version) + 2(length) + 1(type) + 2(seq) = 11 bytes
    constexpr std::size_t HEADER_WIRE_SIZE = 11;
    constexpr std::size_t MAX_PAYLOAD_SIZE =
        (MAX_PACKET_SIZE >= HEADER_WIRE_SIZE) ? (MAX_PACKET_SIZE - HEADER_WIRE_SIZE) : 0;

    // =========================
    // Core types
    // =========================
    using SequenceNumber = uint16_t;

    enum class PacketType : uint8_t {
        Handshake = 0x00,
        ReliableAck = 0x01,
        PlayerAction = 0x02,
        ChatMessage = 0x03,
        GameState = 0x04,
        Heartbeat = 0x05,
        EchoTest = 0x06,
        Unknown = 0xFF
    };

    struct PacketHeader {
        uint32_t       magic;    // PROTOCOL_MAGIC
        uint16_t       version;  // PROTOCOL_VERSION
        uint16_t       length;   // payload length in bytes (excludes this header)
        PacketType     type;     // on wire: 1 byte
        SequenceNumber seq;      // sequence
    };

    enum class ParseError : uint8_t {
        None = 0,
        TooShort = 1,
        BadMagic = 2,
        UnsupportedVer = 3,
        LengthTooLarge = 4
    };

    static_assert(sizeof(PacketType) == 1, "PacketType must be 1 byte");
    static_assert(sizeof(SequenceNumber) == 2, "SequenceNumber must be 2 bytes");

    // =========================
    // Big-endian (network order) helpers
    // =========================
    inline void be_write16(uint8_t* p, uint16_t v) {
        p[0] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[1] = static_cast<uint8_t>(v & 0xFF);
    }
    inline void be_write32(uint8_t* p, uint32_t v) {
        p[0] = static_cast<uint8_t>((v >> 24) & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[3] = static_cast<uint8_t>(v & 0xFF);
    }
    inline void be_write64(uint8_t* p, uint64_t v) {
        p[0] = static_cast<uint8_t>((v >> 56) & 0xFF);
        p[1] = static_cast<uint8_t>((v >> 48) & 0xFF);
        p[2] = static_cast<uint8_t>((v >> 40) & 0xFF);
        p[3] = static_cast<uint8_t>((v >> 32) & 0xFF);
        p[4] = static_cast<uint8_t>((v >> 24) & 0xFF);
        p[5] = static_cast<uint8_t>((v >> 16) & 0xFF);
        p[6] = static_cast<uint8_t>((v >> 8) & 0xFF);
        p[7] = static_cast<uint8_t>(v & 0xFF);
    }
    inline uint16_t be_read16(const uint8_t* p) {
        return static_cast<uint16_t>((uint16_t(p[0]) << 8) | uint16_t(p[1]));
    }
    inline uint32_t be_read32(const uint8_t* p) {
        return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
    }
    inline uint64_t be_read64(const uint8_t* p) {
        return (uint64_t(p[0]) << 56) | (uint64_t(p[1]) << 48) | (uint64_t(p[2]) << 40) | (uint64_t(p[3]) << 32) |
            (uint64_t(p[4]) << 24) | (uint64_t(p[5]) << 16) | (uint64_t(p[6]) << 8) | uint64_t(p[7]);
    }

    // =========================
    // Core header (11 bytes) serialization
    // =========================
    inline void serialize_header(const PacketHeader& h, uint8_t* out11) {
        be_write32(out11 + 0, h.magic);
        be_write16(out11 + 4, h.version);
        be_write16(out11 + 6, h.length);
        out11[8] = static_cast<uint8_t>(h.type);
        be_write16(out11 + 9, h.seq);
    }

    inline std::pair<PacketHeader, ParseError> parse_header(const uint8_t* data, std::size_t len) {
        PacketHeader h{};
        if (len < HEADER_WIRE_SIZE) return { h, ParseError::TooShort };

        h.magic = be_read32(data + 0);
        h.version = be_read16(data + 4);
        h.length = be_read16(data + 6);
        const uint8_t rawType = data[8];
        h.type = static_cast<PacketType>(rawType);
        h.seq = be_read16(data + 9);

        if (h.magic != PROTOCOL_MAGIC)   return { h, ParseError::BadMagic };
        if (h.version != PROTOCOL_VERSION) return { h, ParseError::UnsupportedVer };
        if (h.length > MAX_PAYLOAD_SIZE) return { h, ParseError::LengthTooLarge };
        return { h, ParseError::None };
    }

    inline bool validate_sizes(const PacketHeader& h, std::size_t totalBytesAvailable) {
        if (totalBytesAvailable < HEADER_WIRE_SIZE) return false;
        return (HEADER_WIRE_SIZE + h.length) <= totalBytesAvailable && h.length <= MAX_PAYLOAD_SIZE;
    }

    // =========================
    // Reliable transport subheader (optional, per-packet)
    // Layout: 2(seq) + 2(ack) + 4(ackBitfield) + 1(type) + 8(nonce) = 17 bytes
    // =========================
    struct ReliablePacketHeader {
        SequenceNumber seq;
        SequenceNumber ack;
        uint32_t       ackBitfield;
        PacketType     type;
        uint64_t       nonce;
    };

    constexpr std::size_t RELIABLE_HDR_WIRE_SIZE = 17;

    inline void serialize_reliable_header(const ReliablePacketHeader& rh, uint8_t* out17) {
        be_write16(out17 + 0, rh.seq);
        be_write16(out17 + 2, rh.ack);
        be_write32(out17 + 4, rh.ackBitfield);
        out17[8] = static_cast<uint8_t>(rh.type);
        be_write64(out17 + 9, rh.nonce);
    }

    enum class ReliableParseError : uint8_t {
        None = 0,
        TooShort = 1
    };

    inline std::pair<ReliablePacketHeader, ReliableParseError>
        parse_reliable_header(const uint8_t* data, std::size_t len) {
        ReliablePacketHeader rh{};
        if (len < RELIABLE_HDR_WIRE_SIZE) return { rh, ReliableParseError::TooShort };
        rh.seq = be_read16(data + 0);
        rh.ack = be_read16(data + 2);
        rh.ackBitfield = be_read32(data + 4);
        rh.type = static_cast<PacketType>(data[8]);
        rh.nonce = be_read64(data + 9);
        return { rh, ReliableParseError::None };
    }

    // =========================
    // Reliability tracking state (RFC6298-style RTT/RTO)
    // =========================
    constexpr float RTT_ALPHA = 0.125f;
    constexpr float RTT_BETA = 0.250f;
    constexpr float RTO_K = 4.0f;
    constexpr float DEFAULT_INITIAL_RTT_MS = 200.0f;
    constexpr float MIN_RTO_MS = 100.0f;
    constexpr float MAX_RTO_MS = 3000.0f;
    constexpr int   MAX_PACKET_RETRIES = 10;

    struct ReliablePacket {
        SequenceNumber        sequenceNumber{};
        PacketType            packetType{ PacketType::Unknown };
        uint64_t              nonce{ 0 };
        std::vector<uint8_t>  data;
        uint64_t              timeSentMs{ 0 };
        int                   retries{ 0 };

        ReliablePacket() = default;

        ReliablePacket(SequenceNumber seq, PacketType type, uint64_t nonceVal,
            const std::vector<uint8_t>& payload, uint64_t sentTimeMs)
            : sequenceNumber(seq), packetType(type), nonce(nonceVal),
            data(payload), timeSentMs(sentTimeMs), retries(0) {
        }

        ReliablePacket(SequenceNumber seq, PacketType type, uint64_t nonceVal,
            std::vector<uint8_t>&& payload, uint64_t sentTimeMs)
            : sequenceNumber(seq), packetType(type), nonce(nonceVal),
            data(std::move(payload)), timeSentMs(sentTimeMs), retries(0) {
        }
    };

    struct ReliableConnectionState {
        // --- Sequence management ---
        SequenceNumber nextOutgoingSequenceNumber{ 1 };
        SequenceNumber highestReceivedSequenceNumber{ 0 };
        uint32_t       receivedSequenceBitfield{ 0 };

        // --- RTT / RTO estimation ---
        float smoothedRTT_ms{ DEFAULT_INITIAL_RTT_MS };
        float rttVariance_ms{ DEFAULT_INITIAL_RTT_MS / 2.0f };
        float retransmissionTimeout_ms{ DEFAULT_INITIAL_RTT_MS * 2.0f };
        bool  isFirstRTTSample{ true };

        // --- Reliability tracking ---
        std::list<ReliablePacket> unacknowledgedSentPackets;

        // --- Timing ---
        std::chrono::steady_clock::time_point lastPacketReceivedTime{ std::chrono::steady_clock::now() };
        std::chrono::steady_clock::time_point lastPacketSentTime{ std::chrono::steady_clock::now() };

        // --- Status flags ---
        bool connectionDroppedByMaxRetries{ false };
        bool hasPendingAckToSend{ false };
        bool isConnected{ true };

        // --- Nonce management ---
        uint64_t nextNonce{ 1 };
        uint64_t lastUsedNonce{ 1 };

        // --- Thread safety ---
        mutable std::mutex internalStateMutex;

        // --- Utilities ---
        static inline uint64_t NowMs() {
            return static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count()
                );
        }

        inline bool ShouldDropPacket(int retries) const {
            return retries >= MAX_PACKET_RETRIES;
        }
    };

} // namespace RiftNet::Protocol
