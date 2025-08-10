#pragma once
#include <cstdint>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>
#include <cstring>
#include <algorithm>
#include <cmath>

#include "ReliableConnectionState.hpp"  // brings ReliablePacketHeader, ReliableConnectionState, RTT constants
#include "protocols.hpp"             // SequenceNumber
#include "Logger.hpp"
//#include "RiftNetWire.hpp"              // <-- your header that defines RiftNet::PacketHeader, MAX_PACKET_SIZE, PacketID

using namespace RiftNet::Protocol;

namespace RiftForged::Networking {

    class UDPReliabilityProtocol {
    public:
        // ---- Existing API (kept) -------------------------------------------------
        static std::vector<std::vector<uint8_t>> PrepareOutgoingPackets(
            ReliableConnectionState& state,
            const uint8_t* payload,
            uint32_t payloadSize,
            uint8_t packetType,
            uint64_t nonce);

        static bool ProcessIncomingHeader(
            ReliableConnectionState& state,
            const ReliablePacketHeader& header,
            const uint8_t* packetPayload,
            uint16_t payloadLength,
            std::vector<uint8_t>& outPayload);

        static bool ShouldSendAck(
            ReliableConnectionState& state,
            std::chrono::steady_clock::time_point now);

        static void ProcessRetransmissions(
            ReliableConnectionState& state,
            std::chrono::steady_clock::time_point now,
            const std::function<void(const std::vector<uint8_t>&)>& sendFunc);

        static bool IsConnectionTimedOut(
            const ReliableConnectionState& state,
            const std::chrono::steady_clock::time_point& now,
            int timeoutSeconds);

        // ---- New high-level API (framed wire: [PacketHeader][ReliableHeader][payload]) ----
        // Max body you can fit given the 1024-byte outer cap
        static constexpr uint16_t MaxBodySize();

        // Builds fully framed wires (ready to encrypt+send)
        static std::vector<std::vector<uint8_t>> PrepareOutgoingPacketsFramed(
            ReliableConnectionState& state,
            PacketType packetId,
            const uint8_t* payload,
            uint32_t payloadSize,
            uint64_t nonce);

        // Parses a fully framed wire (after decrypt), applies reliability, and returns PacketID + payload
        static bool ProcessIncomingWire(
            ReliableConnectionState& state,
            const uint8_t* wire,
            uint32_t wireLen,
            PacketType& outPacketId,
            std::vector<uint8_t>& outPayload);

    private:
        // ---- helpers ----
        static inline bool IsSequenceMoreRecent(SequenceNumber s1, SequenceNumber s2);
        static void ApplyRTTSampleUnlocked(ReliableConnectionState& state, float sampleRTT_ms);

        static bool WriteFramedReliablePacket(
            std::vector<uint8_t>& outWire,
            PacketType packetId,
            const ReliablePacketHeader& relHdr,
            const uint8_t* payload, uint16_t payloadLen);

        static bool ReadFramedReliablePacket(
            const uint8_t* wire, uint32_t wireLen,
            PacketHeader& outOuter,
            ReliablePacketHeader& outRel,
            const uint8_t*& outPayload,
            uint16_t& outPayloadLen);
    };

} // namespace RiftForged::Networking
