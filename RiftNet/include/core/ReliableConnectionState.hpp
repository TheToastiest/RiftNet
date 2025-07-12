#pragma once

#include <cstdint>
#include <vector>
#include <chrono>
#include <list>
#include <mutex>
#include <algorithm>
#include <cmath>
#include "ReliablePacketHeader.hpp" // Assumes this defines SequenceNumber
#include "NetworkTypes.hpp"

namespace RiftForged {
    namespace Networking {

        // --- RTT and RTO Constants ---
        const float RTT_ALPHA = 0.125f;
        const float RTT_BETA = 0.250f;
        const float RTO_K = 4.0f;
        const float DEFAULT_INITIAL_RTT_MS = 200.0f;
        const float MIN_RTO_MS = 100.0f;
        const float MAX_RTO_MS = 3000.0f;
        const int MAX_PACKET_RETRIES = 10;
        const uint16_t MAX_PAYLOAD_SIZE = 1200; // Max payload before fragmentation

        // This struct is now a pure data container for a single connection's state.
        struct ReliableConnectionState {
            SequenceNumber nextOutgoingSequenceNumber = 1;

            struct SentPacketInfo {
                SequenceNumber sequenceNumber;
                std::chrono::steady_clock::time_point timeSent;
                std::vector<uint8_t> packetData;
                int retries = 0;
                bool isAckOnly = false; // This flag is important

                // THIS CONSTRUCTOR IS NOW CORRECTED TO MATCH YOUR ORIGINAL
                SentPacketInfo(SequenceNumber seq, const std::vector<uint8_t>& data, bool ackOnlyFlag)
                    : sequenceNumber(seq),
                    timeSent(std::chrono::steady_clock::now()),
                    packetData(data),
                    retries(0),
                    isAckOnly(ackOnlyFlag) {
                }
            };
            std::list<SentPacketInfo> unacknowledgedSentPackets;

            // --- Incoming State ---
            SequenceNumber highestReceivedSequenceNumber = 0;
            uint32_t receivedSequenceBitfield = 0;

            // --- RTT and RTO Calculation ---
            float smoothedRTT_ms = DEFAULT_INITIAL_RTT_MS;
            float rttVariance_ms = DEFAULT_INITIAL_RTT_MS / 2.0f;
            float retransmissionTimeout_ms = DEFAULT_INITIAL_RTT_MS * 2.0f;
            bool isFirstRTTSample = true;

            // --- General Connection State ---
            std::chrono::steady_clock::time_point lastPacketReceivedTime;
            bool connectionDroppedByMaxRetries = false;

            // Constructor
            ReliableConnectionState() {
                lastPacketReceivedTime = std::chrono::steady_clock::now();
            }

            bool ShouldDropPacket(int retries) const {
                return retries >= MAX_PACKET_RETRIES;
            }

            // This logic is now a standalone helper function inside the protocol's .cpp file.
            void ApplyRTTSample(float sampleRTT_ms);
        };

    } // namespace Networking
} // namespace RiftForged