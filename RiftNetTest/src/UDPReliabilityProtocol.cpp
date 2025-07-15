#include "../include/UDPReliabilityProtocol.hpp"
#include <cstring>
#include <algorithm>

namespace RiftForged::Networking {

    namespace {

        inline bool IsSequenceMoreRecent(SequenceNumber s1, SequenceNumber s2) {
            constexpr SequenceNumber halfRange = (static_cast<SequenceNumber>(-1) / 2) + 1;
            return ((s1 > s2) && (s1 - s2 < halfRange)) ||
                ((s2 > s1) && (s2 - s1 >= halfRange));
        }

        inline void ApplyRTTSampleUnlocked(ReliableConnectionState& state, float sampleRTT_ms) {
            if (state.isFirstRTTSample) {
                state.smoothedRTT_ms = sampleRTT_ms;
                state.rttVariance_ms = sampleRTT_ms / 2.0f;
                state.isFirstRTTSample = false;
            }
            else {
                float delta = sampleRTT_ms - state.smoothedRTT_ms;
                state.smoothedRTT_ms += RTT_ALPHA * delta;
                state.rttVariance_ms += RTT_BETA * (std::abs(delta) - state.rttVariance_ms);
            }

            state.retransmissionTimeout_ms = std::clamp(
                state.smoothedRTT_ms + RTO_K * state.rttVariance_ms,
                MIN_RTO_MS, MAX_RTO_MS);
        }
    }

    std::vector<std::vector<uint8_t>> UDPReliabilityProtocol::PrepareOutgoingPackets(
        ReliableConnectionState& state,
        const uint8_t* payload,
        uint32_t payloadSize,
        uint8_t packetType,
        uint64_t nonce)
    {
        std::lock_guard<std::mutex> lock(state.internalStateMutex);
        std::vector<std::vector<uint8_t>> packets;

        ReliablePacketHeader header{
            .sequenceNumber = state.nextOutgoingSequenceNumber++,
            .ackNumber = state.highestReceivedSequenceNumber,
            .ackBitfield = state.receivedSequenceBitfield,
            .packetType = packetType,
            .nonce = nonce
        };

        const size_t headerSize = sizeof(ReliablePacketHeader);
        std::vector<uint8_t> buffer(headerSize + payloadSize);
        std::memcpy(buffer.data(), &header, headerSize);
        if (payload && payloadSize > 0) {
            std::memcpy(buffer.data() + headerSize, payload, payloadSize);
        }

        state.unacknowledgedSentPackets.emplace_back(
            header.sequenceNumber, packetType, nonce, buffer, state.GetCurrentTime()
        );

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
        std::lock_guard<std::mutex> lock(state.internalStateMutex);
        state.lastPacketReceivedTime = std::chrono::steady_clock::now();

        // ACK processing
        for (auto it = state.unacknowledgedSentPackets.begin(); it != state.unacknowledgedSentPackets.end();) {
            const auto& sent = *it;
            bool isAcked = (header.ackNumber == sent.sequenceNumber) ||
                (IsSequenceMoreRecent(header.ackNumber, sent.sequenceNumber) &&
                    ((header.ackBitfield >> (header.ackNumber - sent.sequenceNumber - 1)) & 1));

            if (isAcked) {
                if (sent.retries == 0) {
                    float rtt = static_cast<float>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            state.lastPacketReceivedTime - std::chrono::steady_clock::time_point(std::chrono::milliseconds(sent.timeSent))
                        ).count()
                        );
                    ApplyRTTSampleUnlocked(state, rtt);
                }
                it = state.unacknowledgedSentPackets.erase(it);
            }
            else {
                ++it;
            }
        }

        // Sequence handling
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

    bool UDPReliabilityProtocol::ShouldSendAck(
        ReliableConnectionState& state,
        std::chrono::steady_clock::time_point now)
    {
        std::lock_guard<std::mutex> lock(state.internalStateMutex);
        if (!state.hasPendingAckToSend) return false;

        float rttFraction = std::min(state.smoothedRTT_ms / 4.0f, 20.0f);
        rttFraction = std::max(rttFraction, 5.0f);

        auto sinceLastSend = std::chrono::duration_cast<std::chrono::milliseconds>(now - state.lastPacketSentTime).count();
        return sinceLastSend >= rttFraction;
    }

    void UDPReliabilityProtocol::ProcessRetransmissions(
        ReliableConnectionState& state,
        std::chrono::steady_clock::time_point now,
        const std::function<void(const std::vector<uint8_t>&)>& sendFunc)
    {
        std::lock_guard<std::mutex> lock(state.internalStateMutex);

        for (auto& packet : state.unacknowledgedSentPackets) {
            float elapsed = static_cast<float>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - std::chrono::steady_clock::time_point(std::chrono::milliseconds(packet.timeSent))
                ).count());

            if (elapsed >= state.retransmissionTimeout_ms) {
                if (state.ShouldDropPacket(packet.retries)) {
                    state.connectionDroppedByMaxRetries = true;
                    state.isConnected = false;
                    return;
                }

                sendFunc(packet.data);
                packet.timeSent = state.GetCurrentTime();
                ++packet.retries;

                // Exponential backoff
                state.retransmissionTimeout_ms = std::clamp(
                    state.retransmissionTimeout_ms * 2.0f,
                    MIN_RTO_MS, MAX_RTO_MS
                );
            }
        }
    }

    bool UDPReliabilityProtocol::IsConnectionTimedOut(
        const ReliableConnectionState& state,
        const std::chrono::steady_clock::time_point& now,
        int timeoutSeconds)
    {
        std::lock_guard<std::mutex> lock(state.internalStateMutex);
        return state.connectionDroppedByMaxRetries ||
            std::chrono::duration_cast<std::chrono::seconds>(now - state.lastPacketReceivedTime).count() > timeoutSeconds;
    }

}
