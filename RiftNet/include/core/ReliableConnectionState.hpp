#pragma once

#include <cstdint>
#include <vector>
#include <list>
#include <chrono>
#include <mutex>
#include "NetworkTypes.hpp"

namespace RiftForged {
    namespace Networking {

        // === RTT and RTO Constants ===
        constexpr float RTT_ALPHA = 0.125f;
        constexpr float RTT_BETA = 0.250f;
        constexpr float RTO_K = 4.0f;
        constexpr float DEFAULT_INITIAL_RTT_MS = 200.0f;
        constexpr float MIN_RTO_MS = 100.0f;
        constexpr float MAX_RTO_MS = 3000.0f;
        constexpr int MAX_PACKET_RETRIES = 10;
        constexpr uint16_t MAX_PAYLOAD_SIZE = 1200;

        struct ReliablePacket {
            SequenceNumber sequenceNumber;
            uint8_t packetType;
            uint64_t nonce;
            std::vector<uint8_t> data;
            uint64_t timeSent;
            int retries = 0;  

            ReliablePacket() = default;

            ReliablePacket(SequenceNumber seq, uint8_t type, uint64_t nonceVal,
                const std::vector<uint8_t>& payload, uint64_t sentTime)
                : sequenceNumber(seq), packetType(type), nonce(nonceVal),
                data(payload), timeSent(sentTime), retries(0) {
            }

            ReliablePacket(SequenceNumber seq, uint8_t type, uint64_t nonceVal,
                std::vector<uint8_t>&& payload, uint64_t sentTime)
                : sequenceNumber(seq), packetType(type), nonce(nonceVal),
                data(std::move(payload)), timeSent(sentTime), retries(0) {
            }
        };


        struct ReliableConnectionState {
            // === Sequence Management ===
            SequenceNumber nextOutgoingSequenceNumber = 1;
            SequenceNumber highestReceivedSequenceNumber = 0;
            uint32_t receivedSequenceBitfield = 0;

            // === RTT / RTO Estimation ===
            float smoothedRTT_ms = DEFAULT_INITIAL_RTT_MS;
            float rttVariance_ms = DEFAULT_INITIAL_RTT_MS / 2.0f;
            float retransmissionTimeout_ms = DEFAULT_INITIAL_RTT_MS * 2.0f;
            bool isFirstRTTSample = true;

            // === Reliability Tracking ===
            std::list<ReliablePacket> unacknowledgedSentPackets;

            // === Timing ===
            std::chrono::steady_clock::time_point lastPacketReceivedTime;
            std::chrono::steady_clock::time_point lastPacketSentTime;

            // === Flags & Status ===
            bool connectionDroppedByMaxRetries = false;
            bool hasPendingAckToSend = false;
            bool isConnected = true;

            // === Nonce Management ===
            uint64_t nextNonce = 1;
            uint64_t lastUsedNonce = 1;

            // === Thread Safety ===
            mutable std::mutex internalStateMutex;

            // === Construction ===
            ReliableConnectionState() {
                lastPacketReceivedTime = std::chrono::steady_clock::now();
                lastPacketSentTime = std::chrono::steady_clock::now();
            }

            // === Utility ===
            bool ShouldDropPacket(int retries) const {
                return retries >= MAX_PACKET_RETRIES;
            }

            uint64_t GetCurrentTime() const {
                return std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()
                ).count();
            }
        };

        struct ReliablePacketHeader {
            uint16_t sequenceNumber;
            uint16_t ackNumber;
            uint32_t ackBitfield;
            uint8_t packetType;
            uint64_t nonce;
        };

    } // namespace Networking
} // namespace RiftForged
