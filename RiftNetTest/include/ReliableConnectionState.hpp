#pragma once

#include <cstdint>
#include <vector>
#include <list>
#include <chrono>
#include "ReliableTypes.hpp"
#include "NetworkTypes.hpp"

namespace RiftForged {
    namespace Networking {

        // RTT and RTO Constants
        constexpr float RTT_ALPHA = 0.125f;
        constexpr float RTT_BETA = 0.250f;
        constexpr float RTO_K = 4.0f;
        constexpr float DEFAULT_INITIAL_RTT_MS = 200.0f;
        constexpr float MIN_RTO_MS = 100.0f;
        constexpr float MAX_RTO_MS = 3000.0f;
        constexpr int MAX_PACKET_RETRIES = 10;
        constexpr uint16_t MAX_PAYLOAD_SIZE = 1200;

        struct ReliableConnectionState {
            // === Outgoing Sequence Management ===
            SequenceNumber nextOutgoingSequenceNumber = 1;

            // === Incoming Sequence Tracking ===
            SequenceNumber highestReceivedSequenceNumber = 0;
            uint32_t receivedSequenceBitfield = 0;

            // === RTT / RTO Estimation ===
            float smoothedRTT_ms = DEFAULT_INITIAL_RTT_MS;
            float rttVariance_ms = DEFAULT_INITIAL_RTT_MS / 2.0f;
            float retransmissionTimeout_ms = DEFAULT_INITIAL_RTT_MS * 2.0f;
            bool isFirstRTTSample = true;

            // === Timers and Drop Detection ===
            std::chrono::steady_clock::time_point lastPacketReceivedTime;
            bool connectionDroppedByMaxRetries = false;

            // === Nonce Management for SecureChannel ===
            uint64_t nextNonce = 1;        // Used for sending
            uint64_t lastUsedNonce = 1;    // Used for receiving

            // === Reliability Tracking ===
            struct SentPacketInfo {
                SequenceNumber sequenceNumber;
                std::chrono::steady_clock::time_point timeSent;
                std::vector<uint8_t> packetData;
                int retries = 0;
                bool isAckOnly = false;

                SentPacketInfo(SequenceNumber seq, const std::vector<uint8_t>& data, bool ackOnlyFlag)
                    : sequenceNumber(seq),
                    timeSent(std::chrono::steady_clock::now()),
                    packetData(data),
                    retries(0),
                    isAckOnly(ackOnlyFlag) {
                }
            };

            std::list<SentPacketInfo> unacknowledgedSentPackets;

            // === Logic ===
            bool ShouldDropPacket(int retries) const {
                return retries >= MAX_PACKET_RETRIES;
            }

            ReliableConnectionState() {
                lastPacketReceivedTime = std::chrono::steady_clock::now();
            }
        };

    } // namespace Networking
} // namespace RiftForged
