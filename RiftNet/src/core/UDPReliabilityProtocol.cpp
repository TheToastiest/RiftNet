#include "../../include/core/UDPReliabilityProtocol.hpp"
#include "../../include/core/protocols.hpp"
#include <cstring>
#include <algorithm>
#include <cmath>

using namespace RiftNet::Protocol;

namespace RiftForged::Networking {

    namespace {
        constexpr uint16_t OUTER_HDR_SIZE = static_cast<uint16_t>(HEADER_WIRE_SIZE);
        constexpr uint16_t REL_HDR_SIZE = static_cast<uint16_t>(RELIABLE_HDR_WIRE_SIZE);
    }

    // ----------------- Size math -----------------
    constexpr uint16_t UDPReliabilityProtocol::MaxBodySize() {
        // 1024 - outer(11) - reliable(17)
        return static_cast<uint16_t>(MAX_PACKET_SIZE - OUTER_HDR_SIZE - REL_HDR_SIZE);
    }

    // ----------------- Helpers -------------------
    inline bool UDPReliabilityProtocol::IsSequenceMoreRecent(SequenceNumber s1, SequenceNumber s2) {
        constexpr SequenceNumber halfRange = (static_cast<SequenceNumber>(-1) / 2) + 1;
        return ((s1 > s2) && (s1 - s2 < halfRange)) || ((s2 > s1) && (s2 - s1 >= halfRange));
    }

    void UDPReliabilityProtocol::ApplyRTTSampleUnlocked(ReliableConnectionState& state, float sampleRTT_ms) {
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
        state.retransmissionTimeout_ms = std::clamp(
            state.smoothedRTT_ms + RTO_K * state.rttVariance_ms,
            MIN_RTO_MS, MAX_RTO_MS);
    }

    // ----------------- Wire IO -------------------
    bool UDPReliabilityProtocol::WriteFramedReliablePacket(
        std::vector<uint8_t>& outWire,
        PacketType packetId,
        const ReliablePacketHeader& relHdr,
        const uint8_t* payload, uint16_t payloadLen)
    {
        const uint16_t totalPayload = static_cast<uint16_t>(REL_HDR_SIZE + payloadLen);
        const uint16_t totalWire = static_cast<uint16_t>(OUTER_HDR_SIZE + totalPayload);
        if (totalWire > MAX_PACKET_SIZE) return false;

        outWire.resize(totalWire);

        // Outer header (11 bytes)
        PacketHeader outer{};
        outer.magic = PROTOCOL_MAGIC;
        outer.version = PROTOCOL_VERSION;
        outer.length = totalPayload;         // payload after outer header
        outer.type = packetId;
        outer.seq = relHdr.seq;          // mirror reliable seq for quick filtering

        serialize_header(outer, outWire.data());

        // Reliable header (17 bytes)
        serialize_reliable_header(relHdr, outWire.data() + OUTER_HDR_SIZE);
        ReliablePacketHeader rel = relHdr;
        rel.type = packetId;         // reliable type
        // Body
        if (payloadLen) {
            std::memcpy(outWire.data() + OUTER_HDR_SIZE + REL_HDR_SIZE, payload, payloadLen);
        }
        return true;
    }

    bool UDPReliabilityProtocol::ReadFramedReliablePacket(
        const uint8_t* wire, uint32_t wireLen,
        PacketHeader& outOuter,
        ReliablePacketHeader& outRel,
        const uint8_t*& outPayload,
        uint16_t& outPayloadLen)
    {
        if (wireLen < OUTER_HDR_SIZE + REL_HDR_SIZE) return false;

        // Parse outer header
        auto [hdr, err] = parse_header(wire, wireLen);
        if (err != ParseError::None) return false;
        outOuter = hdr;

        if (wireLen < OUTER_HDR_SIZE + outOuter.length) return false;
        if (outOuter.length < REL_HDR_SIZE) return false;

        // Parse reliable header
        auto [rel, rerr] = parse_reliable_header(wire + OUTER_HDR_SIZE, outOuter.length);
        if (rerr != ReliableParseError::None) return false;
        outRel = rel;

        // Body slice
        outPayloadLen = static_cast<uint16_t>(outOuter.length - REL_HDR_SIZE);
        outPayload = wire + OUTER_HDR_SIZE + REL_HDR_SIZE;
        return true;
    }

    // ----------------- New high-level API -----------------
    std::vector<std::vector<uint8_t>> UDPReliabilityProtocol::PrepareOutgoingPacketsFramed(
        ReliableConnectionState& state,
        PacketType packetTypes,
        const uint8_t* payload,
        uint32_t payloadSize,
        uint64_t nonce)
    {
        std::lock_guard<std::mutex> lock(state.internalStateMutex);
        std::vector<std::vector<uint8_t>> packets;

        // enforce outer cap (optional: chunk if desired)
        if (payloadSize > MaxBodySize()) {
            payloadSize = MaxBodySize(); // clamp for now
        }

        ReliablePacketHeader relHdr{
            /*seq*/        state.nextOutgoingSequenceNumber++,
            /*ack*/        state.highestReceivedSequenceNumber,
            /*ackBitfield*/state.receivedSequenceBitfield,
            /*type*/       packetTypes,            // mirror outer
            /*nonce*/      nonce
        };

        std::vector<uint8_t> wire;
        if (!WriteFramedReliablePacket(wire, packetTypes, relHdr, payload, static_cast<uint16_t>(payloadSize))) {
            return packets;
        }

        // Track inflight (store full wire)
        state.unacknowledgedSentPackets.emplace_back(
            relHdr.seq,
            packetTypes,
            nonce,
            wire,
            ReliableConnectionState::NowMs()
        );

        packets.emplace_back(std::move(wire));
        return packets;
    }

    bool UDPReliabilityProtocol::ProcessIncomingWire(
        ReliableConnectionState& state,
        const uint8_t* wire,
        uint32_t wireLen,
        PacketType& outPacketId,
        std::vector<uint8_t>& outPayload)
    {
        outPayload.clear();
        PacketHeader         outer{};
        ReliablePacketHeader rel{};
        const uint8_t* bodyPtr = nullptr;
        uint16_t             bodyLen = 0;

        if (!ReadFramedReliablePacket(wire, wireLen, outer, rel, bodyPtr, bodyLen)) {
            return false;
        }
        RF_NETWORK_DEBUG("IN: type={} len={} rel(seq={}, ack={}, bits=0x{:08X})",
            static_cast<int>(outPacketId), bodyLen, rel.seq, rel.ack, rel.ackBitfield);

        outPacketId = rel.type; // already PacketType
        return ProcessIncomingHeader(state, rel, bodyPtr, bodyLen, outPayload);
    }

    // ----------------- Existing API (rewired internally) -----------------
    std::vector<std::vector<uint8_t>> UDPReliabilityProtocol::PrepareOutgoingPackets(
        ReliableConnectionState& state,
        const uint8_t* payload,
        uint32_t payloadSize,
        uint8_t packetType,
        uint64_t nonce)
    {
        // Backwards-compatible: build framed using packetType as PacketID
        return PrepareOutgoingPacketsFramed(
            state,
            static_cast<PacketType>(packetType),
            payload, payloadSize, nonce
        );
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

        // ACK processing for outbound queue
        for (auto it = state.unacknowledgedSentPackets.begin();
            it != state.unacknowledgedSentPackets.end(); )
        {
            const auto& sent = *it;

            bool ackMatch = false;
            if (header.ack == sent.sequenceNumber) {
                ackMatch = true;
            }
            else if (IsSequenceMoreRecent(header.ack, sent.sequenceNumber)) {
                const uint16_t diff = static_cast<uint16_t>(header.ack - sent.sequenceNumber);
                if (diff >= 1 && diff <= 32) {
                    ackMatch = ((header.ackBitfield >> (diff - 1)) & 1) != 0;
                }
            }

            if (ackMatch) {
                // RTT sample only from first transmission (classic approach)
                if (sent.retries == 0) {
                    const float rtt = static_cast<float>(
                        std::chrono::duration_cast<std::chrono::milliseconds>(
                            state.lastPacketReceivedTime -
                            std::chrono::steady_clock::time_point(std::chrono::milliseconds(sent.timeSentMs))
                        ).count());
                    ApplyRTTSampleUnlocked(state, rtt);
                }
                it = state.unacknowledgedSentPackets.erase(it);
            }
            else {
                ++it;
            }
        }

        // Sequence window update
        if (IsSequenceMoreRecent(header.seq, state.highestReceivedSequenceNumber)) {
            const uint32_t diff = static_cast<uint32_t>(header.seq - state.highestReceivedSequenceNumber);
            state.receivedSequenceBitfield = (diff < 32) ? (state.receivedSequenceBitfield << diff) : 0u;
            state.receivedSequenceBitfield |= 1u;
            state.highestReceivedSequenceNumber = header.seq;
        }
        else {
            const uint32_t diff = static_cast<uint32_t>(state.highestReceivedSequenceNumber - header.seq);
            if (diff < 32) {
                // duplicate?
                if ((state.receivedSequenceBitfield >> diff) & 1u) {
                    // already seen → ignore body; still accept for ACKing
                    state.hasPendingAckToSend = true;
                    return false;
                }
                state.receivedSequenceBitfield |= (1u << diff);
            }
            else {
                // too old to represent
                return false;
            }
        }

        // Body copy (still compressed at this layer)
        if (packetPayload && payloadLength > 0) {
            outPayload.assign(packetPayload, packetPayload + payloadLength);
        }
        else {
            outPayload.clear();
        }
        const bool isAckOnly = (header.type == PacketType::ReliableAck) && (payloadLength == 0);
        // allow heartbeat to be zero-length too (if you use it)
        const bool isZeroLenCtrl = (payloadLength == 0) &&
            (header.type == PacketType::ReliableAck || header.type == PacketType::Heartbeat);

        if (!isZeroLenCtrl) {
            state.hasPendingAckToSend = true;
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
            const float elapsed = static_cast<float>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - std::chrono::steady_clock::time_point(std::chrono::milliseconds(packet.timeSentMs))
                ).count());

            if (elapsed < state.retransmissionTimeout_ms) continue;

            if (state.ShouldDropPacket(packet.retries)) {
                state.connectionDroppedByMaxRetries = true;
                state.isConnected = false;
                return;
            }

            // resend the full wire
            sendFunc(packet.data);
            packet.timeSentMs = ReliableConnectionState::NowMs();
            ++packet.retries;

            // backoff
            state.retransmissionTimeout_ms = std::clamp(
                state.retransmissionTimeout_ms * 2.0f, MIN_RTO_MS, MAX_RTO_MS);
        }
    }

    bool UDPReliabilityProtocol::IsConnectionTimedOut(
        const ReliableConnectionState& state,
        const std::chrono::steady_clock::time_point& now,
        int timeoutSeconds)
    {
        std::lock_guard<std::mutex> lock(state.internalStateMutex);
        return state.connectionDroppedByMaxRetries ||
            (std::chrono::duration_cast<std::chrono::seconds>(now - state.lastPacketReceivedTime).count() > timeoutSeconds);
    }

} // namespace RiftForged::Networking
