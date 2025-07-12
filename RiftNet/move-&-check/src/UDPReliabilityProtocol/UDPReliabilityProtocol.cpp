#include "../../include/UDPReliabilityProtocol/UDPReliabilityProtocol.h"
#include "../../include/ReliableConnectionState/ReliableConnectionState.h"
#include "../../include/GamePacketHeader/GamePacketHeader.h"
#include <cstring>
#include <algorithm> // For std::min

namespace RiftForged {
    namespace Networking {

        // --- Private Helper Functions (encapsulated from your original free functions) ---
        namespace {
            bool is_sequence_more_recent(SequenceNumber s1, SequenceNumber s2) {
                using T = SequenceNumber;
                const T halfRange = (static_cast<T>(-1) / 2) + 1;
                return ((s1 > s2) && (s1 - s2 < halfRange)) || ((s2 > s1) && (s2 - s1 >= halfRange));
            }

            void apply_rtt_sample(ReliableConnectionState& state, float sampleRTT_ms) {
                if (state.isFirstRTTSample) {
                    state.smoothedRTT_ms = sampleRTT_ms;
                    state.rttVariance_ms = sampleRTT_ms / 2.0f;
                    state.isFirstRTTSample = false;
                }
                else {
                    float rtt_delta = sampleRTT_ms - state.smoothedRTT_ms;
                    state.smoothedRTT_ms += RTT_ALPHA * rtt_delta;
                    state.rttVariance_ms += RTT_BETA * (std::abs(rtt_delta) - state.rttVariance_ms);
                }
                state.retransmissionTimeout_ms = state.smoothedRTT_ms + RTO_K * state.rttVariance_ms;
                state.retransmissionTimeout_ms = std::max(MIN_RTO_MS, std::min(MAX_RTO_MS, state.retransmissionTimeout_ms));
            }
        }

        // --- Public Static Method Implementations ---

        std::vector<std::vector<uint8_t>> UDPReliabilityProtocol::PrepareOutgoingPackets(
            ReliableConnectionState& state,
            const uint8_t* payload,
            uint32_t payloadSize,
            uint8_t flags)
        {
            // This implementation does not yet handle fragmentation.
            // It returns a vector containing just one packet.
            std::vector<std::vector<uint8_t>> resultingPackets;

            GamePacketHeader header(flags); // Use your constructor

            // Use the field names from your GamePacketHeader struct
            header.sequenceNumber = state.nextOutgoingSequenceNumber++;
            header.ackNumber = state.highestReceivedSequenceNumber;

            header.ackBitfield = state.receivedSequenceBitfield;

            size_t packetSize = GetGamePacketHeaderSize() + payloadSize;
            std::vector<uint8_t> packetBuffer(packetSize);
            memcpy(packetBuffer.data(), &header, GetGamePacketHeaderSize());
            if (payload && payloadSize > 0) {
                memcpy(packetBuffer.data() + GetGamePacketHeaderSize(), payload, payloadSize);
            }

            // Use your HasFlag helper for checking the reliable flag
            if (HasFlag(flags, GamePacketFlag::IS_RELIABLE)) {
                // Use your SentPacketInfo constructor
                state.unacknowledgedSentPackets.emplace_back(header.sequenceNumber, packetBuffer, false);
            }

            resultingPackets.push_back(packetBuffer);
            return resultingPackets;
        }

        bool UDPReliabilityProtocol::ProcessIncomingHeader(
            ReliableConnectionState& state, const GamePacketHeader& header,
            const uint8_t* packetPayloadData, uint16_t packetPayloadLength,
            std::vector<uint8_t>& out_reassembledPayload)
        {
            state.lastPacketReceivedTime = std::chrono::steady_clock::now();

            // Process acks received from the remote client
            for (auto it = state.unacknowledgedSentPackets.begin(); it != state.unacknowledgedSentPackets.end();) {
                if (header.ackNumber == it->sequenceNumber || (is_sequence_more_recent(header.ackNumber, it->sequenceNumber) && ((header.ackBitfield >> (header.ackNumber - it->sequenceNumber - 1)) & 1))) {
                    float rttSample = std::chrono::duration<float, std::milli>(state.lastPacketReceivedTime - it->timeSent).count();
                    apply_rtt_sample(state, rttSample);
                    it = state.unacknowledgedSentPackets.erase(it);
                }
                else {
                    ++it;
                }
            }

            // Update our record of packets received by the remote client
            if (is_sequence_more_recent(header.sequenceNumber, state.highestReceivedSequenceNumber)) {
                uint32_t diff = header.sequenceNumber - state.highestReceivedSequenceNumber;
                if (diff < 32) state.receivedSequenceBitfield <<= diff;
                else state.receivedSequenceBitfield = 0;
                state.receivedSequenceBitfield |= 1;
                state.highestReceivedSequenceNumber = header.sequenceNumber;
            }
            else {
                uint32_t diff = state.highestReceivedSequenceNumber - header.sequenceNumber;
                if (diff < 32) {
                    if ((state.receivedSequenceBitfield >> diff) & 1) return false; // Duplicate
                    state.receivedSequenceBitfield |= (1 << diff);
                }
                else {
                    return false; // Too old
                }
            }

            // For now, we pass the payload through directly. Fragmentation logic would go here.
            if (packetPayloadLength > 0) {
                out_reassembledPayload.assign(packetPayloadData, packetPayloadData + packetPayloadLength);
            }

            return true; // Not a duplicate, process payload
        }

        bool UDPReliabilityProtocol::ShouldSendAck(ReliableConnectionState& state) {
            // Implement your logic here. For now, a simple approach is to always be ready.
            // In reality, you might check a timer or if you've received a certain number of packets.
            return true;
        }

        void UDPReliabilityProtocol::ProcessRetransmissions(
            ReliableConnectionState& state,
            std::function<void(const std::vector<uint8_t>&)> sendFunc)
        {
            // Correctly use std::chrono::steady_clock
            auto now = std::chrono::steady_clock::now();

            // Iterate through the list of packets we've sent that are awaiting an ack.
            // Your ReliableConnectionState::unacknowledgedSentPackets works perfectly for this.
            for (auto& sentPacketInfo : state.unacknowledgedSentPackets) {

                // Check if the time since the packet was last sent exceeds the dynamic RTO.
                // Your RTT logic calculates this value and stores it in state.retransmissionTimeout_ms.
                if (std::chrono::duration<float, std::milli>(now - sentPacketInfo.timeSent).count() > state.retransmissionTimeout_ms) {

                    // Your ShouldDropPacket logic checks against MAX_PACKET_RETRIES.
                    if (state.ShouldDropPacket(sentPacketInfo.retries)) {
                        state.connectionDroppedByMaxRetries = true;
                        // Once the connection is marked as dropped, we stop processing more packets for it.
                        return;
                    }

                    // Resend the exact same packet data.
                    sendFunc(sentPacketInfo.packetData);

                    // Update the state for this packet.
                    sentPacketInfo.timeSent = now;
                    sentPacketInfo.retries++;
                }
            }
        }

        bool UDPReliabilityProtocol::IsConnectionTimedOut(const ReliableConnectionState& state, const std::chrono::steady_clock::time_point& now, int timeoutSeconds) {
            if (state.connectionDroppedByMaxRetries) return true;
            return std::chrono::duration_cast<std::chrono::seconds>(now - state.lastPacketReceivedTime).count() > timeoutSeconds;
        }

    } // namespace Networking
} // namespace RiftForged