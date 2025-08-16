#pragma once

#include "../packet/Packet.hpp"
#include <cstdint>
#include <vector>
#include <functional>
#include <chrono>
#include <mutex>
#include <list>

namespace RiftNet::Protocol {

    // =========================
    // Reliability State & Constants
    // =========================

    // State for a single reliable connection. Each connected client will have one of these.
    struct ReliableConnectionState {
        // --- Sequence management ---
        uint16_t nextOutgoingSequence{ 1 };
        uint16_t highestReceivedSequence{ 0 };
        uint32_t  receivedSequenceBitfield{ 0 };

        // --- RTT / RTO estimation ---
        float smoothedRTT_ms{ 100.0f };
        float rttVariance_ms{ 500.0f };
        float retransmissionTimeout_ms{ 250.0f };
        bool  isFirstRTTSample{ true };

        // --- Reliability tracking ---
        struct SentPacket {
            uint16_t sequence;
            std::chrono::steady_clock::time_point timeSent;
            std::vector<uint8_t> data; // The fully constructed packet data
            int retries{ 0 };
        };
        std::list<SentPacket> unacknowledgedPackets;

        // --- Timing & Status ---
        std::chrono::steady_clock::time_point lastPacketReceivedTime{ std::chrono::steady_clock::now() };
        bool hasPendingAckToSend{ false };

        // --- Thread safety ---
        mutable std::mutex stateMutex;
    };


    /**
     * @class UDPReliabilityProtocol
     * @brief A stateless utility class providing functions to manage a reliable connection.
     * All methods operate on an external ReliableConnectionState object.
     */
    class UDPReliabilityProtocol {
    public:
        /**
         * @brief Processes an incoming reliable header, updating the connection state.
         * @param state The connection state to modify.
         * @param header The reliability header from an incoming packet.
         * @return True if the packet is new and should be processed, false if it's a duplicate or out of order.
         */
        static bool ProcessIncomingHeader(
            ReliableConnectionState& state,
            const ReliabilityPacketHeader& header);

        /**
         * @brief Prepares a fully formed, reliable packet for sending.
         * @param state The connection state to use for sequence numbers and acks.
         * @param payload The application data to send.
         * @param payloadSize The size of the application data.
         * @return A byte vector containing the fully constructed packet (GeneralHeader + ReliabilityHeader + Payload).
         */
        static std::vector<uint8_t> PrepareOutgoingPacket(
            ReliableConnectionState& state,
            const uint8_t* payload,
            uint32_t payloadSize);

        /**
         * @brief Checks for and handles any packets that need to be retransmitted.
         * @param state The connection state to check.
         * @param now The current time.
         * @param sendFunc A callback function to send the retransmitted packet data.
         */
        static void ProcessRetransmissions(
            ReliableConnectionState& state,
            std::chrono::steady_clock::time_point now,
            const std::function<void(const std::vector<uint8_t>&)>& sendFunc);

        /**
         * @brief Checks if the connection has timed out.
         * @param state The connection state.
         * @param now The current time.
         * @param timeout The duration after which a connection is considered timed out.
         * @return True if the connection has timed out, false otherwise.
         */
        static bool IsConnectionTimedOut(
            const ReliableConnectionState& state,
            std::chrono::steady_clock::time_point now,
            std::chrono::seconds timeout);
    };

} // namespace RiftNet::Protocol
