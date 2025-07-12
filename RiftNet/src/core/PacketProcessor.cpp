// src/core/PacketProcessor.cpp
#include "../../include/core/PacketProcessor.hpp"
#include "../../include/core/NetworkTypes.hpp"
#include <iostream>

namespace RiftNet
{
    PacketProcessor::PacketProcessor()
    {
        // Initialize as needed
    }

    bool PacketProcessor::ProcessIncomingPacket(const uint8_t* data, size_t length)
    {
        if (length < sizeof(PacketHeader))
        {
            std::cerr << "[PacketProcessor] Invalid packet size: " << length << std::endl;
            return false;
        }

        PacketHeader header;
        std::memcpy(&header, data, sizeof(PacketHeader));

        if (length - sizeof(PacketHeader) != header.payloadSize)
        {
            std::cerr << "[PacketProcessor] Payload size mismatch." << std::endl;
            return false;
        }

        std::cout << "[PacketProcessor] Packet ID: " << header.packetId
            << " | Payload Size: " << header.payloadSize << std::endl;

        // TODO: Pass to RiftHandler, or store for test/demo
        return true;
    }
}
