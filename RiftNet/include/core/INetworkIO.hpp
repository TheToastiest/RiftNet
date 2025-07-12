// File: INetworkIO.h
#pragma once

#include "NetworkEndpoint.hpp"
#include "../platform/OverlappedIOContext.hpp"
#include <string>
#include <cstdint>
#include <vector>

namespace RiftForged {
    namespace Networking {

        class INetworkIOEvents; // Forward declaration

        class INetworkIO {
        public:
            virtual ~INetworkIO() = default;

            /**
             * @brief Initializes the network IO layer.
             * @param listenIp The IP address to listen on.
             * @param listenPort The port to listen on.
             * @param eventHandler Pointer to the event handler that will process network events.
             * @return True if initialization was successful, false otherwise.
             */
            virtual bool Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) = 0;

            /**
             * @brief Starts the network IO layer (e.g., starts worker threads, posts initial receives).
             * @return True if starting was successful, false otherwise.
             */
            virtual bool Start() = 0;

            /**
             * @brief Stops the network IO layer (e.g., stops worker threads, closes sockets).
             */
            virtual void Stop() = 0;

            /**
             * @brief Sends raw data to the specified recipient.
             * @param recipient The network endpoint to send data to.
             * @param data Pointer to the buffer containing the data to send.
             * @param size The size of the data to send in bytes.
             * @return True if the send operation was successfully queued/initiated, false otherwise.
             */
            virtual bool SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) = 0;

            /**
             * @brief Checks if the network IO layer is currently running.
             * @return True if running, false otherwise.
             */
            virtual bool IsRunning() const = 0;


            // These context management methods are for more advanced scenarios where the PacketHandler
            // might want to control the receive context lifecycle. For the initial refactor,
            // UDPSocketAsync can manage its receive contexts internally after an OnRawDataReceived call.
            // You can uncomment and implement these if that level of control becomes necessary.
            /*
            virtual OverlappedIOContext* GetFreeReceiveContext() = 0;
            virtual void ReturnReceiveContext(OverlappedIOContext* pContext) = 0;
            virtual bool PostReceive(OverlappedIOContext* pRecvContext) = 0;
            */
        };

    } // namespace Networking
} // namespace RiftForged