// File: INetworkIOEvents.h
#pragma once

#include "../NetworkEndpoint/NetworkEndpoint.h"
#include "../OverlappedIOContext/OverlappedIOContext.h"
#include <cstdint>
#include <string>

namespace RiftForged {
    namespace Networking {

        class INetworkIOEvents {
        public:
            virtual ~INetworkIOEvents() = default;

            /**
             * @brief Called by the INetworkIO layer when a raw datagram is successfully received.
             * @param sender The network endpoint from which the data was received.
             * @param data Pointer to the buffer containing the received data.
             * @param size The size of the received data in bytes.
             * @param context The OverlappedIOContext used for this receive operation.
             * The INetworkIO layer is responsible for managing (e.g., re-posting or returning to pool)
             * this context after this callback returns, unless specified otherwise by a return value.
             */
            virtual void OnRawDataReceived(const NetworkEndpoint& sender,
                const uint8_t* data,
                uint32_t size,
                OverlappedIOContext* context) = 0;

            /**
             * @brief Called by the INetworkIO layer when an asynchronous send operation completes.
             * @param context The OverlappedIOContext associated with this send operation.
             * This context typically contains the recipient information and the buffer.
             * @param success True if the send operation reported success, false otherwise.
             * @param bytesSent The number of bytes reported as sent by the operation.
             */
            virtual void OnSendCompleted(OverlappedIOContext* context,
                bool success,
                uint32_t bytesSent) = 0;

            /**
             * @brief Called when a critical network error occurs that isn't tied to a specific operation context.
             * @param errorMessage A description of the error.
             */
            virtual void OnNetworkError(const std::string& errorMessage, int errorCode = 0) = 0;

            // Optional: If UDPSocketAsync needs to notify about its own state changes more explicitly
            // virtual void OnNetworkIOShutdown() = 0;
        };

    } // namespace Networking
} // namespace RiftForged