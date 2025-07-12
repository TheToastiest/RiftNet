#pragma once

#include <vector>
#include <functional>
#include <chrono>

namespace RiftForged {
    namespace Networking {

        // Forward declarations
        struct ReliableConnectionState;
        struct GamePacketHeader;

        /**
         * @struct UDPReliabilityProtocol
         * @brief Encapsulates the logic for the reliability layer, using your advanced features.
         */
        struct UDPReliabilityProtocol {
            // Prepares one or more packets (if fragmentation is needed) from a single payload.
            static std::vector<std::vector<uint8_t>> PrepareOutgoingPackets(
                ReliableConnectionState& state,
                const uint8_t* payload,
                uint32_t payloadSize,
                uint8_t flags);

            // Processes an incoming header and returns the payload to process (if any).
            // It will handle re-assembly of fragments.
            static bool ProcessIncomingHeader(
                ReliableConnectionState& state,
                const GamePacketHeader& header,
                const uint8_t* packetPayloadData,
                uint16_t packetPayloadLength,
                std::vector<uint8_t>& out_reassembledPayload
            );

            static bool ShouldSendAck(ReliableConnectionState& state);

            // Checks for packets that need retransmission.
            static void ProcessRetransmissions(
                ReliableConnectionState& state,
                std::function<void(const std::vector<uint8_t>&)> sendFunc
            );

            // Checks if the connection has timed out.
            static bool IsConnectionTimedOut(
                const ReliableConnectionState& state,
                const std::chrono::steady_clock::time_point& now,
                int timeoutSeconds
            );
        };

    } // namespace Networking
} // namespace RiftForged