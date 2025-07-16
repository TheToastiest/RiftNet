// File: INetworkIOEvents.h
#pragma once

#include "NetworkEndpoint.hpp"
#include "../platform/OverlappedIOContext.hpp"
#include <cstdint>
#include <string>

namespace RiftForged {
    namespace Networking {

        class INetworkIOEvents {
        public:
            virtual ~INetworkIOEvents() = default;

            virtual void OnRawDataReceived(const NetworkEndpoint& sender,
                const uint8_t* data,
                uint32_t size,
                OverlappedIOContext* context) = 0;

            virtual void OnSendCompleted(OverlappedIOContext* context,
                bool success,
                uint32_t bytesSent) = 0;
ram errorMessage A description of the error.
             */
            virtual void OnNetworkError(const std::string& errorMessage, int errorCode = 0) = 0;

            // virtual void OnNetworkIOShutdown() = 0;
        };

    } // namespace Networking
} // namespace RiftForged
