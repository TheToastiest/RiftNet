#pragma once
#include "../../include/NetworkEndpoint/NetworkEndpoint.h" // This type is owned by Network
#include <cstdint>

namespace RiftForged {
    namespace Networking {

        // This interface defines the contract for any class that wants to
        // process application payloads received by the Network library.
        class IApplicationPayloadHandler {
        public:
            virtual ~IApplicationPayloadHandler() = default;

            // Called by UDPPacketHandler after it has processed reliability
            // and stripped the packet header.
            virtual void ProcessPayload(
                const NetworkEndpoint& sender,
                const uint8_t* payload,
                uint32_t size
            ) = 0;
        };

    } // namespace Networking
} // namespace RiftForged