#pragma once

#include "../packet/Packet.hpp"
#include "../UDPReliabilityProtocol/UDPReliabilityProtocol.hpp"
#include <vector>
#include <cstdint>

namespace RiftNet::Protocol {

    /**
     * @class PacketFactory
     * @brief A stateless utility class for creating and parsing network packets.
     * This class handles the serialization of packet headers and payloads into byte buffers.
     */
    class PacketFactory {
    public:
        /**
         * @brief Parses a raw byte buffer to identify the packet type and extract headers.
         * @param buffer The raw data received from the socket.
         * @param size The size of the raw data buffer.
         * @param outGeneralHeader The parsed GeneralPacketHeader.
         * @param outReliabilityHeader The parsed ReliabilityPacketHeader (if present).
         * @param outPayload A pointer to the start of the payload data within the buffer.
         * @param outPayloadSize The size of the payload data.
         * @return True if parsing was successful, false otherwise.
         */
        static bool ParsePacket(
            const uint8_t* buffer,
            uint32_t size,
            GeneralPacketHeader& outGeneralHeader,
            ReliabilityPacketHeader& outReliabilityHeader,
            const uint8_t*& outPayload,
            uint32_t& outPayloadSize
        );

        /**
         * @brief Creates a simple packet that has no reliability header or payload.
         * @param type The type of the packet (e.g., Heartbeat, Disconnect).
         * @return A byte vector containing the serialized packet.
         */
        static std::vector<uint8_t> CreateSimplePacket(PacketType type);

        /**
         * @brief Creates a reliable data packet.
         * @param reliabilityState The state of the connection to generate sequence/ack numbers.
         * @param payload The application data to send.
         * @param payloadSize The size of the application data.
         * @return A byte vector containing the serialized packet.
         */
        static std::vector<uint8_t> CreateReliableDataPacket(
            ReliableConnectionState& reliabilityState,
            const uint8_t* payload,
            uint32_t payloadSize
        );

        /**
         * @brief Creates an unreliable data packet.
         * @param payload The application data to send.
         * @param payloadSize The size of the application data.
         * @return A byte vector containing the serialized packet.
         */
        static std::vector<uint8_t> CreateUnreliableDataPacket(
            const uint8_t* payload,
            uint32_t payloadSize
        );
    };

} // namespace RiftNet::Protocol
    