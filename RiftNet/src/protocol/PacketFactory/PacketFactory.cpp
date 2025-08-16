#include "pch.h"

#include "PacketFactory.hpp"
#include <cstring>

namespace RiftNet::Protocol {

    bool PacketFactory::ParsePacket(
        const uint8_t* buffer,
        uint32_t size,
        GeneralPacketHeader& outGeneralHeader,
        ReliabilityPacketHeader& outReliabilityHeader,
        const uint8_t*& outPayload,
        uint32_t& outPayloadSize)
    {
        if (size < sizeof(GeneralPacketHeader)) {
            return false; // Buffer is too small for even the general header.
        }

        // 1. Read the General Header
        memcpy(&outGeneralHeader, buffer, sizeof(GeneralPacketHeader));
        const uint8_t* currentPtr = buffer + sizeof(GeneralPacketHeader);
        uint32_t remainingSize = size - sizeof(GeneralPacketHeader);

        // 2. Check for and Read the Reliability Header
        if (outGeneralHeader.Type == PacketType::Data_Reliable) {
            if (remainingSize < sizeof(ReliabilityPacketHeader)) {
                return false; // Not enough data for the required reliability header.
            }
            memcpy(&outReliabilityHeader, currentPtr, sizeof(ReliabilityPacketHeader));
            currentPtr += sizeof(ReliabilityPacketHeader);
            remainingSize -= sizeof(ReliabilityPacketHeader);
        }
        else {
            // Not a reliable packet, so zero out the reliability header for safety.
            memset(&outReliabilityHeader, 0, sizeof(ReliabilityPacketHeader));
        }

        // 3. The rest of the data is the payload.
        outPayload = currentPtr;
        outPayloadSize = remainingSize;

        return true;
    }

    std::vector<uint8_t> PacketFactory::CreateSimplePacket(PacketType type)
    {
        std::vector<uint8_t> packet(sizeof(GeneralPacketHeader));
        GeneralPacketHeader* header = reinterpret_cast<GeneralPacketHeader*>(packet.data());
        header->Type = type;
        return packet;
    }

    std::vector<uint8_t> PacketFactory::CreateReliableDataPacket(
        ReliableConnectionState& reliabilityState,
        const uint8_t* payload,
        uint32_t payloadSize)
    {
        // The UDPReliabilityProtocol already has the logic to build the full packet
        // including the general and reliability headers. We can just call it directly.
        return UDPReliabilityProtocol::PrepareOutgoingPacket(reliabilityState, payload, payloadSize);
    }

    std::vector<uint8_t> PacketFactory::CreateUnreliableDataPacket(
        const uint8_t* payload,
        uint32_t payloadSize)
    {
        size_t totalSize = sizeof(GeneralPacketHeader) + payloadSize;
        std::vector<uint8_t> packet(totalSize);

        GeneralPacketHeader* header = reinterpret_cast<GeneralPacketHeader*>(packet.data());
        header->Type = PacketType::Data_Unreliable;

        if (payloadSize > 0) {
            memcpy(packet.data() + sizeof(GeneralPacketHeader), payload, payloadSize);
        }

        return packet;
    }

} // namespace RiftNet::Protocol
