#pragma once

#include "../../include/core/UDPReliabilityProtocol.hpp"
#include "../../include/core/ReliableConnectionState.hpp"
#include "../../include/core/ReliablePacketHeader.hpp"
#include <cstring>
#include <algorithm>

namespace RiftForged {
    namespace Networking {

        namespace {

            inline bool IsSequenceMoreRecent(SequenceNumber s1, SequenceNumber s2) {
                constexpr SequenceNumber halfRange = (static_cast<SequenceNumber>(-1) / 2) + 1;
                return ((s1 > s2) && (s1 - s2 < halfRange)) || ((s2 > s1) && (s2 - s1 >= halfRange));
            }

            inline void ApplyRTTSample(ReliableConnectionState& state, float sampleRTT_ms) {
                if (state.isFirstRTTSample) {
                    state.smoothedRTT_ms = sampleRTT_ms;
                    state.rttVariance_ms = sampleRTT_ms / 2.0f;
                    state.isFirstRTTSample = false;
                }
                else {
                    float rttDelta = sampleRTT_ms - state.smoothedRTT_ms;
                    state.smoothedRTT_ms += RTT_ALPHA * rttDelta;
                    state.rttVariance_ms += RTT_BETA * (std::abs(rttDelta) - state.rttVariance_ms);
                }
                state.retransmissionTimeout_ms = std::clamp(
                    state.smoothedRTT_ms + RTO_K * state.rttVariance_ms,
                    MIN_RTO_MS, MAX_RTO_MS
                );
            }
        }

        std::vector<std::vector<uint8_t>> UDPReliabilityProtocol::PrepareOutgoingPackets(
            ReliableConnectionState& state,
            const uint8_t* payload,
            uint32_t payloadSize,
            uint8_t flags)
        {
            std::vector<std::vector<uint8_t>> packets;

            ReliablePacketHeader header{
                .sequenceNumber = state.nextOutgoingSequenceNumber++,
                .ackNumber = state.highestReceivedSequenceNumber,
                .ackBitfield = state.receivedSequenceBitfield
            };

            const size_t headerSize = sizeof(ReliablePacketHeader);
            const size_t packetSize = headerSize + payloadSize;
            std::vector<uint8_t> buffer(packetSize);

            std::memcpy(buffer.data(), &header, headerSize);
            if (payload && payloadSize > 0) {
                std::memcpy(buffer.data() + headerSize, payload, payloadSize);
            }

            if (flags & 0x01) { // Placeholder: replace with HasFlag(flags, GamePacketFlag::IS_RELIABLE)
                state.unacknowledgedSentPackets.emplace_back(header.sequenceNumber, buffer, false);
            }

            packets.push_back(std::move(buffer));
            return packets;
        }

        bool UDPReliabilityProtocol::ProcessIncomingHeader(
            ReliableConnectionState& state,
            const ReliablePacketHeader& header,
            const uint8_t* packetPayload,
            uint16_t payloadLength,
            std::vector<uint8_t>& outPayload)
        {
            state.lastPacketReceivedTime = std::chrono::steady_clock::now();

            for (auto it = state.unacknowledgedSentPackets.begin(); it != state.unacknowledgedSentPackets.end();) {
                const auto seq = it->sequenceNumber;
                bool isAcked = (header.ackNumber == seq) ||
                    (IsSequenceMoreRecent(header.ackNumber, seq) &&
                        ((header.ackBitfield >> (header.ackNumber - seq - 1)) & 1));

                if (isAcked) {
                    float rttSample = std::chrono::duration<float, std::milli>(state.lastPacketReceivedTime - it->timeSent).count();
                    ApplyRTTSample(state, rttSample);
                    it = state.unacknowledgedSentPackets.erase(it);
                }
                else {
                    ++it;
                }
            }

            if (IsSequenceMoreRecent(header.sequenceNumber, state.highestReceivedSequenceNumber)) {
                uint32_t diff = header.sequenceNumber - state.highestReceivedSequenceNumber;
                state.receivedSequenceBitfield = (diff < 32) ? state.receivedSequenceBitfield << diff : 0;
                state.receivedSequenceBitfield |= 1;
                state.highestReceivedSequenceNumber = header.sequenceNumber;
            }
            else {
                uint32_t diff = state.highestReceivedSequenceNumber - header.sequenceNumber;
                if (diff < 32) {
                    if ((state.receivedSequenceBitfield >> diff) & 1) return false;
                    state.receivedSequenceBitfield |= (1 << diff);
                }
                else {
                    return false;
                }
            }

            if (payloadLength > 0) {
                outPayload.assign(packetPayload, packetPayload + payloadLength);
            }

            return true;
        }

        bool UDPReliabilityProtocol::ShouldSendAck(ReliableConnectionState&) {
            return true; // TODO: Add timer or trigger threshold later
        }

        void UDPReliabilityProtocol::ProcessRetransmissions(
            ReliableConnectionState& state,
            const std::function<void(const std::vector<uint8_t>&)>& sendFunc)
        {
            auto now = std::chrono::steady_clock::now();

            for (auto& packet : state.unacknowledgedSentPackets) {
                if (std::chrono::duration<float, std::milli>(now - packet.timeSent).count() > state.retransmissionTimeout_ms) {
                    if (state.ShouldDropPacket(packet.retries)) {
                        state.connectionDroppedByMaxRetries = true;
                        return;
                    }

                    sendFunc(packet.packetData);
                    packet.timeSent = now;
                    ++packet.retries;
                }
            }
        }

        bool UDPReliabilityProtocol::IsConnectionTimedOut(
            const ReliableConnectionState& state,
            const std::chrono::steady_clock::time_point& now,
            int timeoutSeconds)
        {
            return state.connectionDroppedByMaxRetries ||
                std::chrono::duration_cast<std::chrono::seconds>(now - state.lastPacketReceivedTime).count() > timeoutSeconds;
        }

    } // namespace Networking
} // namespace RiftForged
