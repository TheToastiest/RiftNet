#pragma once

#include "ReliableConnectionState.hpp"
#include "ReliableTypes.hpp"
#include <vector>
#include <cstdint>
#include <functional>
#include <chrono>

namespace RiftForged::Networking {

    class UDPReliabilityProtocol {
    public:
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

        static bool ShouldSendAck(ReliableConnectionState& state,
            std::chrono::steady_clock::time_point now);

        static void ProcessRetransmissions(
            ReliableConnectionState& state,
            std::chrono::steady_clock::time_point now,
            const std::function<void(const std::vector<uint8_t>&)>& sendFunc);

        static bool IsConnectionTimedOut(
            const ReliableConnectionState& state,
            const std::chrono::steady_clock::time_point& now,
            int timeoutSeconds);
    };
}
