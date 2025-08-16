#include "pch.h"
#include "UDPReliabilityProtocol.hpp"
#include <algorithm> // For std::clamp
#include <cmath>     // For std::abs

namespace RiftNet::Protocol {

    // =========================
    // Private Helper Functions
    // =========================

    namespace { // Anonymous namespace for internal linkage

        // Helper to check if sequence number s1 is more recent than s2.
        // This correctly handles wrapping around the 16-bit sequence number space.
        bool IsSequenceMoreRecent(uint16_t s1, uint16_t s2) {
            constexpr uint16_t halfRange = (UINT16_MAX / 2) + 1;
            return ((s1 > s2) && (s1 - s2 < halfRange)) || ((s2 > s1) && (s2 - s1 > halfRange));
        }

        // Updates the RTT (Round-Trip Time) and RTO (Retransmission Timeout) using a standard algorithm.
        void ApplyRTTSample(ReliableConnectionState& state, float sampleRTT_ms) {
            constexpr float RTT_ALPHA = 0.125f;
            constexpr float RTT_BETA = 0.250f;
            constexpr float RTO_K = 4.0f;
            constexpr float MIN_RTO_MS = 100.0f;
            constexpr float MAX_RTO_MS = 3000.0f;

            if (state.isFirstRTTSample) {
                state.smoothedRTT_ms = sampleRTT_ms;
                state.rttVariance_ms = sampleRTT_ms / 2.0f;
                state.isFirstRTTSample = false;
            }
            else {
                const float delta = sampleRTT_ms - state.smoothedRTT_ms;
                state.smoothedRTT_ms += RTT_ALPHA * delta;
                state.rttVariance_ms += RTT_BETA * (std::abs(delta) - state.rttVariance_ms);
            }
            // FIX: Wrap std::clamp in parentheses to prevent macro expansion on Windows.
            state.retransmissionTimeout_ms = (std::clamp)(
                state.smoothedRTT_ms + RTO_K * state.rttVariance_ms,
                MIN_RTO_MS, MAX_RTO_MS);
        }

    } // end anonymous namespace


    // =========================
    // Public API Implementation
    // =========================

    bool UDPReliabilityProtocol::ProcessIncomingHeader(
        ReliableConnectionState& state,
        const ReliabilityPacketHeader& header)
    {
        std::lock_guard<std::mutex> lock(state.stateMutex);
        state.lastPacketReceivedTime = std::chrono::steady_clock::now();

        // --- 1. Process Acks and Update RTT ---
        // Iterate through our list of sent packets that are waiting for an ack.
        for (auto it = state.unacknowledgedPackets.begin(); it != state.unacknowledgedPackets.end(); ) {
            bool isAcked = false;
            if (it->sequence == header.ack) {
                isAcked = true;
            }
            else if (IsSequenceMoreRecent(header.ack, it->sequence)) {
                const uint16_t diff = header.ack - it->sequence;
                if (diff > 0 && diff <= 32) {
                    if ((header.ack_bitfield >> (diff - 1)) & 1) {
                        isAcked = true;
                    }
                }
            }

            if (isAcked) {
                // This packet has been acknowledged. Calculate RTT if it wasn't a retransmission.
                if (it->retries == 0) {
                    auto rtt = std::chrono::duration_cast<std::chrono::microseconds>(state.lastPacketReceivedTime - it->timeSent).count();
                    ApplyRTTSample(state, static_cast<float>(rtt) / 1000.0f);
                }
                it = state.unacknowledgedPackets.erase(it);
            }
            else {
                ++it;
            }
        }

        // --- 2. Update Our Receive Window ---
        // Check if the incoming packet is new or a duplicate.
        if (IsSequenceMoreRecent(header.sequence, state.highestReceivedSequence)) {
            uint16_t diff = header.sequence - state.highestReceivedSequence;
            state.receivedSequenceBitfield <<= diff;
            state.receivedSequenceBitfield |= 1; // Set the bit for the new sequence
            state.highestReceivedSequence = header.sequence;
        }
        else {
            uint16_t diff = state.highestReceivedSequence - header.sequence;
            if (diff > 0 && diff <= 32) {
                // Check if this is a duplicate packet we've already seen.
                if ((state.receivedSequenceBitfield >> diff) & 1) {
                    return false; // It's a duplicate, ignore its payload.
                }
                // It's an old packet that arrived out of order, mark it as received.
                state.receivedSequenceBitfield |= (1 << diff);
            }
            else {
                return false; // Packet is too old, ignore.
            }
        }

        state.hasPendingAckToSend = true;
        return true; // The packet is new and should be processed by the application.
    }

    std::vector<uint8_t> UDPReliabilityProtocol::PrepareOutgoingPacket(
        ReliableConnectionState& state,
        const uint8_t* payload,
        uint32_t payloadSize)
    {
        std::lock_guard<std::mutex> lock(state.stateMutex);

        // --- 1. Construct Headers ---
        GeneralPacketHeader generalHeader{};
        generalHeader.Type = PacketType::Data_Reliable;

        ReliabilityPacketHeader reliableHeader{};
        reliableHeader.sequence = state.nextOutgoingSequence++;
        reliableHeader.ack = state.highestReceivedSequence;
        reliableHeader.ack_bitfield = state.receivedSequenceBitfield;

        // --- 2. Assemble the Packet ---
        size_t totalSize = sizeof(generalHeader) + sizeof(reliableHeader) + payloadSize;
        std::vector<uint8_t> packetData(totalSize);

        uint8_t* writePtr = packetData.data();
        memcpy(writePtr, &generalHeader, sizeof(generalHeader));
        writePtr += sizeof(generalHeader);
        memcpy(writePtr, &reliableHeader, sizeof(reliableHeader));
        writePtr += sizeof(reliableHeader);
        if (payloadSize > 0) {
            memcpy(writePtr, payload, payloadSize);
        }

        // --- 3. Track for Retransmission ---
        state.unacknowledgedPackets.push_back({
            reliableHeader.sequence,
            std::chrono::steady_clock::now(),
            packetData, // Store the full packet data for easy retransmission
            0
            });

        // We sent an ack, so we don't have one pending anymore.
        state.hasPendingAckToSend = false;

        return packetData;
    }

    void UDPReliabilityProtocol::ProcessRetransmissions(
        ReliableConnectionState& state,
        std::chrono::steady_clock::time_point now,
        const std::function<void(const std::vector<uint8_t>&)>& sendFunc)
    {
        std::lock_guard<std::mutex> lock(state.stateMutex);

        for (auto& packet : state.unacknowledgedPackets) {
            auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - packet.timeSent).count();

            if (elapsed_ms >= state.retransmissionTimeout_ms) {
                // Timeout detected, retransmit the packet.
                sendFunc(packet.data);

                // Update state for this packet
                packet.timeSent = now;
                packet.retries++;

                // Apply exponential backoff to the RTO for this connection
                // FIX: Wrap std::min in parentheses to prevent macro expansion on Windows.
                state.retransmissionTimeout_ms = (std::min)(state.retransmissionTimeout_ms * 2.0f, 3000.0f);
            }
        }
    }

    bool UDPReliabilityProtocol::IsConnectionTimedOut(
        const ReliableConnectionState& state,
        std::chrono::steady_clock::time_point now,
        std::chrono::seconds timeout)
    {
        std::lock_guard<std::mutex> lock(state.stateMutex);
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - state.lastPacketReceivedTime);
        return elapsed > timeout;
    }

} // namespace RiftNet::Protocol
