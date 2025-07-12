#pragma once

#include "ReliableConnectionState.hpp"
#include "ReliableTypes.hpp"
#include <vector>
#include <cstdint>
#include <functional>

namespace RiftForged {
    namespace Networking {

        class UDPReliabilityProtocol {
        public:

            // Prepare outgoing packets (e.g. split/fragmentation later, reliable state update)
            static std::vector<std::vector<uint8_t>> PrepareOutgoingPackets(
                ReliableConnectionState& state,
                const uint8_t* payload,
                uint32_t payloadSize,
                uint8_t flags);

            // Process incoming packet header and return true if payload should be processed
            static bool ProcessIncomingHeader(
                ReliableConnectionState& state,
                const ReliablePacketHeader& header,
                const uint8_t* packetPayload,
                uint16_t payloadLength,
                std::vector<uint8_t>& outPayload);

            // Determine if an ack-only packet should be sent
            static bool ShouldSendAck(ReliableConnectionState& state);

            // Perform retransmissions for unacked reliable packets
            static void ProcessRetransmissions(
                ReliableConnectionState& state,
                const std::function<void(const std::vector<uint8_t>&)>& sendFunc);

            // Check if connection timed out
            static bool IsConnectionTimedOut(
                const ReliableConnectionState& state,
                const std::chrono::steady_clock::time_point& now,
                int timeoutSeconds);
        };

    } // namespace Networking
} // namespace RiftForged
