// File: UDPSocketAsync.h
// RiftForged Game Engine
// Copyright (C) 2023 RiftForged Team
// Description: Header file for the UDPSocketAsync class, which handles asynchronous UDP socket operations using IOCP.
// This class will implement the INetworkIO interface.

#pragma once

#include <string>           // For std::string
#include <vector>           // For std::vector
#include <thread>           // For std::thread
#include <atomic>           // For std::atomic
#include <mutex>            // For std::mutex
#include <deque>            // For std::deque
#include <memory>           // For std::unique_ptr

// Winsock specific includes
#define WIN32_LEAN_AND_MEAN // Excludes less common headers from Windows.h
#ifndef NOMINMAX
#define NOMINMAX            // Prevents min/max macros from conflicting with std::min/max
#endif
#include <Winsock2.h>       // Core Winsock functions
#include <Ws2tcpip.h>       // For inet_pton, etc.
#pragma comment(lib, "Ws2_32.lib") // Link with Winsock librarya

// Project-specific includes
#include "../core/INetworkIO.hpp"           // Definition of the interface we are implementing
#include "../core/NetworkEndpoint.hpp"      // Defines NetworkEndpoint struct
#include "OverlappedIOContext.hpp"  // Defines OverlappedIOContext struct

// Constants for the UDP buffer and pending receives.
// These could be made configurable in a production system.
const int DEFAULT_UDP_BUFFER_SIZE_IOCP = 4096; // Default buffer size for UDP datagrams
const int MAX_PENDING_RECEIVES_IOCP = 200;     // Maximum number of concurrent WSARecvFrom operations

namespace RiftForged {
    namespace Networking {

        // UDPSocketAsync implements INetworkIO to provide asynchronous UDP socket
        // operations using Windows I/O Completion Ports (IOCP).
        class UDPSocketAsync : public INetworkIO {
        public:
            // Default constructor. Initialization of specific IP/port and event handler
            // happens via the Init method of the INetworkIO interface.
            UDPSocketAsync();

            // Destructor. Ensures proper cleanup of socket resources and worker threads.
            ~UDPSocketAsync() override; // 'override' keyword ensures it correctly overrides a base class virtual function.

            // Disable copy and assignment to prevent accidental copying of complex
            // internal state (sockets, handles, threads, mutexes).
            UDPSocketAsync(const UDPSocketAsync&) = delete;
            UDPSocketAsync& operator=(const UDPSocketAsync&) = delete;

            // --- INetworkIO Interface Implementation ---

            /**
             * @brief Initializes the UDP socket and associates it with an IOCP.
             * @param listenIp The IP address to bind the socket to (e.g., "0.0.0.0" for all interfaces).
             * @param listenPort The port number to listen on.
             * @param eventHandler A pointer to the object that will receive network I/O events (e.g., UDPPacketHandler).
             * @return True if initialization is successful, false otherwise.
             */
            bool Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) override;

            /**
             * @brief Starts the network I/O operations, including creating worker threads
             * and posting initial receive operations.
             * @return True if operations are successfully started, false otherwise.
             */
            bool Start() override;

            /**
             * @brief Stops all network I/O operations, signals worker threads to exit,
             * closes handles, and cleans up resources.
             */
            void Stop() override;

            /**
             * @brief Sends raw data asynchronously to a specified recipient.
             * @param recipient The target network endpoint (IP address and port).
             * @param data A pointer to the raw byte buffer to send.
             * @param size The number of bytes to send.
             * @return True if the send operation was successfully initiated, false otherwise.
             * The actual completion (success/failure) will be reported via OnSendCompleted.
             */
            bool SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) override;


            /**
            * @brief Sends a raw packet to the recipient endpoint.
            * Provided for convenience when using lambdas or callbacks.
            */
            bool SendTo(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) {
                return SendData(recipient, data, size);
            }

            /**
             * @brief Checks if the network I/O is currently running.
             * @return True if running, false otherwise.
             */
            bool IsRunning() const override;

        private:
            // The main loop for IOCP worker threads, processing completed I/O operations.
            void WorkerThread();

            /**
             * @brief Posts an asynchronous receive operation (WSARecvFrom) to the IOCP.
             * This function attempts to get a free context from the pool and queue a receive.
             * @param pRecvContext A pointer to an OverlappedIOContext to use for this receive operation.
             * @return True if the operation was successfully posted, false otherwise.
             */
            bool PostReceiveInternal(OverlappedIOContext* pRecvContext);

            /**
             * @brief Retrieves a free OverlappedIOContext from the internal pool for a new receive operation.
             * @return A pointer to a free context, or nullptr if the pool is exhausted.
             */
            OverlappedIOContext* GetFreeReceiveContextInternal();

            /**
             * @brief Returns a used OverlappedIOContext to the internal pool, making it available for reuse.
             * @param pContext The context to return.
             */
            void ReturnReceiveContextInternal(OverlappedIOContext* pContext);

            // --- Member Variables ---
            std::string m_listenIp;           // The IP address the socket is bound to.
            uint16_t m_listenPort;            // The port number the socket is listening on.
            INetworkIOEvents* m_eventHandler; // Pointer to the handler that receives events (e.g., received data, errors).

            SOCKET m_socket;                  // The Winsock UDP socket handle.
            HANDLE m_iocpHandle;              // Handle to the I/O Completion Port.

            std::vector<std::thread> m_workerThreads; // Collection of threads processing IOCP completions.
            std::atomic<bool> m_isRunning;            // Atomic flag to control the lifetime of worker threads.

            // Receive context pooling for efficient reuse of OVERLAPPED structures and buffers.
            std::vector<std::unique_ptr<OverlappedIOContext>> m_receiveContextPool; // Owns the memory for all contexts.
            std::deque<OverlappedIOContext*> m_freeReceiveContexts;                 // Queue of currently available contexts.
            std::mutex m_receiveContextMutex;                                       // Protects access to m_freeReceiveContexts.
        };

    } // namespace Networking
} // namespace RiftForged