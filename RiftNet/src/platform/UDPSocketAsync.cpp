// File: UDPSocketAsync.cpp
// RiftForged Game Development Team
// Copyright (c) 2023-2025 RiftForged Game Development Team
// Description: Implements INetworkIO for asynchronous UDP socket operations using IOCP.

#include "../../include/platform/UDPSocketAsync.hpp"
#include "../../include/core/INetworkIOEvents.hpp"
#include "../../include/platform/OverlappedIOContext.hpp"
#include "../../include/core/Logger.hpp"
#include <stdexcept>             // For std::system_error, std::invalid_argument
#include <vector>
#include <cstring>               // For ZeroMemory, memcpy
#include <sstream>               // For std::ostringstream
#include <winsock2.h>            // For WSAGetLastError, closesocket, etc.
#include <ws2tcpip.h>            // For inet_pton, etc.
#include <system_error>          // For std::system_error

// DetermineNumWorkerThreads: A helper function to decide how many IOCP worker threads to create.
// This is currently a static function. If thread count needs to be dynamically configurable per instance,
// this logic would become a member function or configuration parameter.
static unsigned int DetermineNumWorkerThreads() {
    // It's common to use a number of threads equal to the number of CPU cores
    // or (2 * num_cores + 1) for IOCP. Here, a simple 4 is chosen as a fallback
    // if hardware_concurrency is 0 or for simplicity.
    unsigned int num_threads = std::thread::hardware_concurrency();
    if (num_threads == 0) num_threads = 4; // Fallback for systems where hardware_concurrency returns 0.
    return num_threads;
}
// This constant determines the actual number of worker threads used by this instance.
static const unsigned int NUM_WORKER_THREADS_TO_CREATE = DetermineNumWorkerThreads();


namespace RiftForged {
    namespace Networking {

        // Constructor: Initializes member variables to a safe default state.
        // IP/Port and the event handler are set via the Init() method.
        UDPSocketAsync::UDPSocketAsync()
            : m_listenIp(""),
            m_listenPort(0),
            m_eventHandler(nullptr), // Must be set via Init() before use.
            m_socket(INVALID_SOCKET),
            m_iocpHandle(NULL), // NULL is for HANDLE, INVALID_HANDLE_VALUE for CreateIoCompletionPort
            m_isRunning(false)
        {
            RF_NETWORK_INFO("UDPSocketAsync: Constructor called.");
        }

        // Destructor: Ensures all resources are properly released by calling Stop().
        UDPSocketAsync::~UDPSocketAsync() {
            RF_NETWORK_INFO("UDPSocketAsync: Destructor called. Attempting to stop...");
            Stop(); // Call Stop to clean up Winsock, threads, and handles.
        }

        // IsRunning: Returns the current running state of the socket.
        bool UDPSocketAsync::IsRunning() const {
            // Use memory_order_acquire to ensure visibility of `m_isRunning` changes made by other threads.
            return m_isRunning.load(std::memory_order_acquire);
        }

        // Init: Initializes the Winsock environment, creates and binds the UDP socket,
        // and sets up the I/O Completion Port.
        bool UDPSocketAsync::Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) {
            RF_NETWORK_INFO("UDPSocketAsync: Initializing for {}...", listenIp.c_str(), listenPort);

            // Prevent re-initialization if already running.
            if (m_isRunning.load(std::memory_order_relaxed)) {
                RF_NETWORK_WARN("UDPSocketAsync: Already initialized and potentially running. Please call Stop() first.");
                return false;
            }
            // Ensure a valid event handler is provided.
            if (!eventHandler) {
                RF_NETWORK_CRITICAL("UDPSocketAsync: Initialization failed - INetworkIOEvents handler is null.");
                return false;
            }

            m_eventHandler = eventHandler; // Store the event handler.
            m_listenIp = listenIp;         // Store the listen IP.
            m_listenPort = listenPort;     // Store the listen port.

            // Initialize Winsock.
            WSADATA wsaData;
            int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
            if (result != 0) {
                RF_NETWORK_CRITICAL("UDPSocketAsync: WSAStartup failed with error: {}", result);
                m_eventHandler->OnNetworkError("WSAStartup failed", result);
                return false;
            }
            RF_NETWORK_INFO("UDPSocketAsync: WSAStartup successful.");

            // Create a UDP socket with OVERLAPPED I/O flag for IOCP.
            m_socket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
            if (m_socket == INVALID_SOCKET) {
                int errorCode = WSAGetLastError();
                RF_NETWORK_CRITICAL("UDPSocketAsync: WSASocket() failed with error: {}", errorCode);
                m_eventHandler->OnNetworkError("WSASocket failed", errorCode);
                WSACleanup();
                return false;
            }
            RF_NETWORK_INFO("UDPSocketAsync: Socket created successfully (Socket ID: {}).", m_socket); // Use %llu for SOCKET type

            // Prepare the server address structure.
            sockaddr_in serverAddr;
            serverAddr.sin_family = AF_INET;
            serverAddr.sin_port = htons(m_listenPort); // Convert port to network byte order.
            // Convert IP string to binary form.
            if (inet_pton(AF_INET, m_listenIp.c_str(), &serverAddr.sin_addr) != 1) {
                int errorCode = GetLastError(); // GetLastError for non-Winsock specific errors like invalid argument.
                RF_NETWORK_CRITICAL("UDPSocketAsync: inet_pton failed for IP {}. Error: {}", m_listenIp.c_str(), errorCode);
                m_eventHandler->OnNetworkError("inet_pton failed for listen IP", errorCode);
                closesocket(m_socket); m_socket = INVALID_SOCKET;
                WSACleanup();
                return false;
            }

            // Bind the socket to the specified IP address and port.
            if (bind(m_socket, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
                int errorCode = WSAGetLastError();
                RF_NETWORK_CRITICAL("UDPSocketAsync: bind() failed with error: {}", errorCode);
                m_eventHandler->OnNetworkError("bind failed", errorCode);
                closesocket(m_socket); m_socket = INVALID_SOCKET;
                WSACleanup();
                return false;
            }
            RF_NETWORK_INFO("UDPSocketAsync: Socket bound successfully to {}.", m_listenIp.c_str(), m_listenPort);

            // Create the I/O Completion Port.
            m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
            if (m_iocpHandle == NULL) {
                int errorCode = GetLastError();
                RF_NETWORK_CRITICAL("UDPSocketAsync: CreateIoCompletionPort (for IOCP itself) failed with error: %d", errorCode);
                m_eventHandler->OnNetworkError("CreateIoCompletionPort failed (IOCP handle)", errorCode);
                closesocket(m_socket); m_socket = INVALID_SOCKET;
                WSACleanup();
                return false;
            }
            RF_NETWORK_DEBUG("UDPSocketAsync: IOCP created successfully.");

            // Associate the UDP socket with the created I/O Completion Port.
            // The completion key (last parameter) can be used to distinguish different sockets,
            // but for a single UDP socket, it's often 0 or ignored.
            if (CreateIoCompletionPort((HANDLE)m_socket, m_iocpHandle, (ULONG_PTR)0, 0) == NULL) {
                int errorCode = GetLastError();
                RF_NETWORK_CRITICAL("UDPSocketAsync: CreateIoCompletionPort (associating socket) failed with error: %d", errorCode);
                m_eventHandler->OnNetworkError("CreateIoCompletionPort failed (socket association)", errorCode);
                CloseHandle(m_iocpHandle); m_iocpHandle = NULL;
                closesocket(m_socket); m_socket = INVALID_SOCKET;
                WSACleanup();
                return false;
            }
            RF_NETWORK_DEBUG("UDPSocketAsync: Socket associated with IOCP successfully.");

            // Pre-allocate and initialize a pool of OverlappedIOContext objects for receive operations.
            // This avoids dynamic allocations during high-frequency receive events.
            try {
                m_receiveContextPool.reserve(MAX_PENDING_RECEIVES_IOCP);
                for (int i = 0; i < MAX_PENDING_RECEIVES_IOCP; ++i) {
                    m_receiveContextPool.emplace_back(std::make_unique<OverlappedIOContext>(IOOperationType::Recv, DEFAULT_UDP_BUFFER_SIZE_IOCP));
                    m_freeReceiveContexts.push_back(m_receiveContextPool.back().get());
                }
                RF_NETWORK_INFO("UDPSocketAsync: Receive context pool initialized with %zu contexts.", m_freeReceiveContexts.size());
            }
            catch (const std::bad_alloc& e) {
                RF_NETWORK_CRITICAL("UDPSocketAsync: Failed to allocate memory for receive context pool: %s", e.what());
                m_eventHandler->OnNetworkError("Failed to allocate receive context pool", 0);
                if (m_iocpHandle) { CloseHandle(m_iocpHandle); m_iocpHandle = NULL; }
                if (m_socket != INVALID_SOCKET) { closesocket(m_socket); m_socket = INVALID_SOCKET; }
                WSACleanup();
                return false;
            }

            RF_NETWORK_INFO("UDPSocketAsync: Initialization successful.");
            return true;
        }

        // Start: Begins the network I/O operations by launching worker threads and posting initial receives.
        bool UDPSocketAsync::Start() {
            if (m_socket == INVALID_SOCKET || m_iocpHandle == NULL) {
                RF_NETWORK_ERROR("UDPSocketAsync: Cannot start. Socket not initialized or IOCP handle is null.");
                return false;
            }
            if (!m_eventHandler) {
                RF_NETWORK_CRITICAL("UDPSocketAsync: Cannot start. Event handler is null (was Init called and successful?).");
                return false;
            }
            if (m_isRunning.load(std::memory_order_relaxed)) {
                RF_NETWORK_WARN("UDPSocketAsync: Already running.");
                return true; // Or false if trying to start an already running socket is an error.
            }

            RF_NETWORK_INFO("UDPSocketAsync: Starting network operations...");
            // Set running flag *before* starting threads to ensure they see it.
            m_isRunning = true;

            // Create worker threads to process IOCP completions.
            m_workerThreads.reserve(NUM_WORKER_THREADS_TO_CREATE);
            for (unsigned int i = 0; i < NUM_WORKER_THREADS_TO_CREATE; ++i) {
                try {
                    m_workerThreads.emplace_back(&UDPSocketAsync::WorkerThread, this);
                }
                catch (const std::system_error& e) {
                    RF_NETWORK_CRITICAL("UDPSocketAsync: Failed to create worker thread %u: %s", i, e.what());
                    m_eventHandler->OnNetworkError("Failed to create worker thread", i); // Pass thread index or similar context
                    Stop(); // Attempt to clean up already created resources.
                    return false;
                }
            }
            RF_NETWORK_INFO("UDPSocketAsync: %zu worker threads created.", m_workerThreads.size());

            // Post initial receive operations to prime the IOCP.
            int successfullyPosted = 0;
            for (int i = 0; i < MAX_PENDING_RECEIVES_IOCP; ++i) {
                OverlappedIOContext* pContext = GetFreeReceiveContextInternal();
                if (!pContext) {
                    RF_NETWORK_ERROR("UDPSocketAsync: Start - Failed to get free receive context for initial post %d.", i);
                    break; // No more contexts to post.
                }
                if (!PostReceiveInternal(pContext)) {
                    RF_NETWORK_ERROR("UDPSocketAsync: Start - Failed to post initial receive operation %d. Error: %d", i, WSAGetLastError());
                    // PostReceiveInternal already returns context to pool on failure.
                }
                else {
                    successfullyPosted++;
                }
            }

            if (successfullyPosted == 0 && MAX_PENDING_RECEIVES_IOCP > 0) {
                RF_NETWORK_CRITICAL("UDPSocketAsync: CRITICAL - Failed to post ANY initial receive operations. Cannot start.");
                m_eventHandler->OnNetworkError("Failed to post any initial receive operations", 0);
                Stop(); // Clean up if no receives could be posted.
                return false;
            }
            RF_NETWORK_INFO("UDPSocketAsync: Successfully posted %d initial receive operations. Server is listening.", successfullyPosted);
            return true;
        }

        // Stop: Halts all network I/O activities and releases resources.
        void UDPSocketAsync::Stop() {
            // Atomically set m_isRunning to false and check its previous state.
            // Use memory_order_acq_rel for strong synchronization.
            if (!m_isRunning.exchange(false, std::memory_order_acq_rel)) {
                RF_NETWORK_INFO("UDPSocketAsync: Stop called but already not running or stop initiated.");
                return;
            }
            RF_NETWORK_INFO("UDPSocketAsync: Stopping network operations...");

            // Signal worker threads to exit by posting sentinel completion packets to the IOCP.
            if (m_iocpHandle != NULL) {
                for (size_t i = 0; i < m_workerThreads.size(); ++i) {
                    // Post a completion status with NULL context (a "poison pill") to signal threads to exit.
                    PostQueuedCompletionStatus(m_iocpHandle, 0, 0, NULL);
                }
                RF_NETWORK_DEBUG("UDPSocketAsync: Shutdown signals posted to IOCP for %zu worker threads.", m_workerThreads.size());
            }

            // Close the socket. This will cause any pending WSARecvFrom/WSASendTo operations
            // to complete with an error (e.g., WSA_OPERATION_ABORTED), allowing contexts to be processed.
            if (m_socket != INVALID_SOCKET) {
                SOCKET tempSock = m_socket; // Store for use after invalidating member
                m_socket = INVALID_SOCKET;  // Mark as invalid to prevent further use
                // Shutdown can help unblock threads, though closesocket is often sufficient for UDP.
                shutdown(tempSock, SD_BOTH); // SD_BOTH indicates no more sends or receives.
                closesocket(tempSock);
                RF_NETWORK_INFO("UDPSocketAsync: Socket closed.");
            }

            // Join worker threads to ensure they have all exited cleanly.
            RF_NETWORK_INFO("UDPSocketAsync: Joining worker threads...");
            for (auto& thread : m_workerThreads) {
                if (thread.joinable()) { // Check if the thread is joinable (i.e., still running or not joined yet).
                    thread.join();
                }
            }
            m_workerThreads.clear(); // Clear the vector of thread objects.
            RF_NETWORK_INFO("UDPSocketAsync: All worker threads joined.");

            // Close the IOCP handle.
            if (m_iocpHandle != NULL) {
                CloseHandle(m_iocpHandle);
                m_iocpHandle = NULL;
                RF_NETWORK_INFO("UDPSocketAsync: IOCP handle closed.");
            }

            // Clear the receive context pool. unique_ptr's destructor will free memory.
            {
                std::lock_guard<std::mutex> lock(m_receiveContextMutex);
                m_freeReceiveContexts.clear(); // Clear deque of raw pointers.
            }
            m_receiveContextPool.clear(); // unique_ptrs automatically delete owned objects.
            RF_NETWORK_DEBUG("UDPSocketAsync: Receive context pool cleared.");

            // Clean up Winsock.
            WSACleanup();
            RF_NETWORK_INFO("UDPSocketAsync: Network operations stopped successfully.");
            // Optional: if (m_eventHandler) m_eventHandler->OnNetworkIOShutdown(); // Notify higher layers of shutdown.
        }

        // GetFreeReceiveContextInternal: Retrieves a context from the pool for a receive operation.
        OverlappedIOContext* UDPSocketAsync::GetFreeReceiveContextInternal() {
            std::lock_guard<std::mutex> lock(m_receiveContextMutex);
            if (m_freeReceiveContexts.empty()) {
                RF_NETWORK_WARN("UDPSocketAsync: No free receive contexts available in pool. Consider increasing MAX_PENDING_RECEIVES_IOCP.");
                return nullptr;
            }
            OverlappedIOContext* pContext = m_freeReceiveContexts.front();
            m_freeReceiveContexts.pop_front();
            return pContext;
        }

        // ReturnReceiveContextInternal: Returns a context to the pool.
        void UDPSocketAsync::ReturnReceiveContextInternal(OverlappedIOContext* pContext) {
            if (!pContext) return; // Prevent null pointer issues.
            std::lock_guard<std::mutex> lock(m_receiveContextMutex);
            m_freeReceiveContexts.push_back(pContext); // Add back to the end of the deque.
        }

        // PostReceiveInternal: Initiates an asynchronous receive operation.
        bool UDPSocketAsync::PostReceiveInternal(OverlappedIOContext* pRecvContext) {
            if (!pRecvContext) {
                RF_NETWORK_CRITICAL("UDPSocketAsync::PostReceiveInternal: ERROR - pRecvContext is null.");
                return false;
            }
            if (m_socket == INVALID_SOCKET) { // Check if socket is still valid.
                RF_NETWORK_ERROR("UDPSocketAsync::PostReceiveInternal: ERROR - Invalid socket, cannot post receive for context %p.", (void*)pRecvContext);
                ReturnReceiveContextInternal(pRecvContext); // Return context immediately if socket is invalid.
                return false;
            }

            pRecvContext->ResetForReceive(); // Reset OVERLAPPED and prepare buffers for a new receive.

            DWORD dwFlags = 0; // Flags for WSARecvFrom; usually 0 for UDP.
            // Call WSARecvFrom to post the asynchronous receive operation.
            int result = WSARecvFrom(
                m_socket,                           // The socket to receive on.
                &(pRecvContext->wsaBuf),            // WSABUF structure pointing to the receive buffer.
                1,                                  // Number of WSABUF structures.
                NULL,                               // lpNumberOfBytesRecvd (NULL for overlapped operations).
                &dwFlags,                           // lpFlags.
                (SOCKADDR*)&(pRecvContext->remoteAddrNative), // Buffer for the sender's address.
                &(pRecvContext->remoteAddrNativeLen),       // Size of the sender address buffer.
                &(pRecvContext->overlapped),        // The OVERLAPPED structure.
                NULL                                // lpCompletionRoutine (NULL for IOCP).
            );

            if (result == SOCKET_ERROR) {
                int errorCode = WSAGetLastError();
                // WSA_IO_PENDING is not an error; it means the operation is pending completion on the IOCP.
                if (errorCode != WSA_IO_PENDING) {
                    RF_NETWORK_ERROR("UDPSocketAsync::PostReceiveInternal: WSARecvFrom failed immediately with error: %d for context: %p.", errorCode, (void*)pRecvContext);
                    ReturnReceiveContextInternal(pRecvContext); // Return context if the post itself fails.
                    return false;
                }
                // If WSA_IO_PENDING, the operation will eventually complete via IOCP.
                // RF_NETWORK_TRACE("UDPSocketAsync::PostReceiveInternal: WSARecvFrom pending for context %p.", (void*)pRecvContext);
            }
            // If result is 0 (immediate completion), a completion packet is still queued to the IOCP.
            // RF_NETWORK_TRACE("UDPSocketAsync::PostReceiveInternal: WSARecvFrom completed immediately for context %p.", (void*)pRecvContext);

            return true;
        }

        // WorkerThread: The main loop for each IOCP worker thread.
        // It continuously waits for completed I/O operations and dispatches them.
        void UDPSocketAsync::WorkerThread() {
            std::ostringstream oss_thread_id_start;
            oss_thread_id_start << std::this_thread::get_id();
            RF_NETWORK_INFO("UDPSocketAsync: Worker thread started (ID: %s)", oss_thread_id_start.str().c_str());

            OverlappedIOContext* pIoContext = nullptr;
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0; // Not heavily used for single-socket IOCP, but necessary parameter.

            while (m_isRunning.load(std::memory_order_acquire)) { // Loop while the socket is running.
                pIoContext = nullptr; // Reset for each iteration.
                bytesTransferred = 0;

                // Wait for a completed I/O operation from the IOCP.
                // A timeout allows the loop to periodically check `m_isRunning`.
                BOOL bSuccess = GetQueuedCompletionStatus(
                    m_iocpHandle,                  // Handle to the IOCP.
                    &bytesTransferred,             // Number of bytes transferred.
                    &completionKey,                // Completion key (the value passed to CreateIoCompletionPort).
                    (LPOVERLAPPED*)&pIoContext,    // Pointer to the OVERLAPPED structure of the completed operation.
                    100                            // Timeout in milliseconds.
                );

                if (!bSuccess) { // GetQueuedCompletionStatus returned FALSE (error or timeout).
                    DWORD errorCode = GetLastError();
                    if (errorCode == WAIT_TIMEOUT) {
                        continue; // Expected timeout, loop to check `m_isRunning` again.
                    }

                    // An actual error occurred on GetQueuedCompletionStatus.
                    RF_NETWORK_ERROR("UDPSocketAsync: WorkerThread - GetQueuedCompletionStatus returned FALSE. WinError: %d. Context: %p, OpType: %d",
                        errorCode, (void*)pIoContext, (pIoContext ? static_cast<int>(pIoContext->operationType) : -1));

                    if (pIoContext != NULL) { // If a context is associated with the error (operation failed).
                        if (pIoContext->operationType == IOOperationType::Recv) {
                            RF_NETWORK_WARN("WorkerThread: Failed Recv Op in GQCS. Error: %d. Context %p.", errorCode, (void*)pIoContext);
                            // Attempt to re-post the receive operation if the server is still running.
                            if (m_isRunning.load(std::memory_order_relaxed)) {
                                if (!PostReceiveInternal(pIoContext)) { // PostReceiveInternal handles returning context on its own failure.
                                    RF_NETWORK_CRITICAL("WorkerThread: CRITICAL - Failed to re-post Recv context %p after I/O error %d or server stopping. Returning to pool.", (void*)pIoContext, errorCode);
                                }
                                else {
                                    // Successfully re-posted, context is back in use.
                                }
                            }
                            else { // Server is stopping, return context to pool.
                                ReturnReceiveContextInternal(pIoContext);
                            }
                        }
                        else if (pIoContext->operationType == IOOperationType::Send) {
                            RF_NETWORK_ERROR("WorkerThread: Failed Send Op in GQCS. Error: %d. Context %p.", errorCode, (void*)pIoContext);
                            // Notify event handler about failed send.
                            if (m_eventHandler) m_eventHandler->OnSendCompleted(pIoContext, false, 0);
                            delete pIoContext; // Send contexts are dynamically allocated.
                        }
                        else {
                            RF_NETWORK_ERROR("WorkerThread: Unknown operation type in failed GQCS. Context %p.", (void*)pIoContext);
                            // Fallback cleanup for unknown types; assume dynamic allocation for safety.
                            delete pIoContext;
                        }
                        pIoContext = nullptr; // Context has been handled.
                    }
                    else { // pIoContext is NULL - GQCS failed without a context (e.g., IOCP handle closed).
                        RF_NETWORK_ERROR("UDPSocketAsync: WorkerThread - GetQueuedCompletionStatus failed without a context. WinError: %d. Assuming shutdown.", errorCode);
                        if (m_eventHandler) m_eventHandler->OnNetworkError("GQCS failed without context", errorCode);
                        // If the IOCP handle is invalid or abandoned, the thread should exit.
                        if (!m_isRunning.load(std::memory_order_relaxed) || errorCode == ERROR_ABANDONED_WAIT_0 || errorCode == ERROR_INVALID_HANDLE) {
                            break; // Exit loop if server is stopping or critical error occurred.
                        }
                    }
                    continue; // Continue to next iteration after handling error.
                }

                // GetQueuedCompletionStatus succeeded (bSuccess is TRUE).
                if (pIoContext == NULL) {
                    // This is an explicit shutdown signal (PostQueuedCompletionStatus with NULL context).
                    RF_NETWORK_INFO("UDPSocketAsync: WorkerThread received NULL context (explicit shutdown signal). Exiting.");
                    break; // Exit the while loop.
                }

                // A successful I/O operation has completed.
                switch (pIoContext->operationType) {
                case IOOperationType::Recv:
                {
                    if (bytesTransferred > 0) {
                        NetworkEndpoint sender_endpoint;
                        char senderIpBuffer[INET_ADDRSTRLEN]; // Standard buffer size for IPv4 addresses.

                        // Convert binary address to string IP and port.
                        if (inet_ntop(AF_INET, &(pIoContext->remoteAddrNative.sin_addr), senderIpBuffer, INET_ADDRSTRLEN)) {
                            sender_endpoint.ipAddress = senderIpBuffer;
                            sender_endpoint.port = ntohs(pIoContext->remoteAddrNative.sin_port);

                            // Hand off the received raw data to the INetworkIOEvents handler.
                            if (m_eventHandler) {
                                m_eventHandler->OnRawDataReceived(sender_endpoint,
                                    reinterpret_cast<const uint8_t*>(pIoContext->buffer.data()),
                                    bytesTransferred,
                                    pIoContext); // Pass context for informational purposes.
                            }
                        }
                        else { // inet_ntop failed.
                            int ntopErrorCode = WSAGetLastError();
                            RF_NETWORK_ERROR("UDPSocketAsync: WorkerThread - inet_ntop failed for received packet. Error: %d.", ntopErrorCode);
                            if (m_eventHandler) m_eventHandler->OnNetworkError("inet_ntop failed", ntopErrorCode);
                        }
                    }
                    else if (bytesTransferred == 0) {
                        // For UDP, receiving 0 bytes means an empty datagram was sent.
                        RF_NETWORK_WARN("UDPSocketAsync: WorkerThread - Received 0 bytes on a Recv operation (UDP). Context: %p.", (void*)pIoContext);
                        // Still, identify the sender and potentially pass a 0-byte payload.
                        NetworkEndpoint sender_endpoint;
                        char senderIpBuffer[INET_ADDRSTRLEN];
                        if (inet_ntop(AF_INET, &(pIoContext->remoteAddrNative.sin_addr), senderIpBuffer, INET_ADDRSTRLEN)) {
                            sender_endpoint.ipAddress = senderIpBuffer;
                            sender_endpoint.port = ntohs(pIoContext->remoteAddrNative.sin_port);
                            if (m_eventHandler) {
                                m_eventHandler->OnRawDataReceived(sender_endpoint, nullptr, 0, pIoContext);
                            }
                        }
                        else {
                            int ntopErrorCode = WSAGetLastError();
                            RF_NETWORK_ERROR("UDPSocketAsync: WorkerThread - inet_ntop failed for 0-byte received packet. Error: %d.", ntopErrorCode);
                            if (m_eventHandler) m_eventHandler->OnNetworkError("inet_ntop failed for 0-byte packet", ntopErrorCode);
                        }
                    }

                    // Always re-post the receive context if the server is still running.
                    if (m_isRunning.load(std::memory_order_relaxed)) {
                        if (!PostReceiveInternal(pIoContext)) {
                            RF_NETWORK_CRITICAL("UDPSocketAsync: WorkerThread - CRITICAL: Failed to re-post WSARecvFrom. Context: %p. Error: %d",
                                (void*)pIoContext, WSAGetLastError());
                            // If PostReceiveInternal fails, it returns the context to the pool.
                        }
                        else {
                            // RF_NETWORK_TRACE("WorkerThread: Successfully re-posted Recv context %p.", (void*)pIoContext);
                        }
                    }
                    else { // Server is stopping, return context to pool.
                        ReturnReceiveContextInternal(pIoContext);
                    }
                    pIoContext = nullptr; // Context has been handled (re-posted or returned).
                    break;
                } // End of case IOOperationType::Recv

                case IOOperationType::Send:
                {
                    // Notify the event handler about the send completion.
                    if (m_eventHandler) {
                        m_eventHandler->OnSendCompleted(pIoContext, true, bytesTransferred); // true for success (bSuccess was true).
                    }
                    delete pIoContext; // Send contexts are dynamically allocated with `new`, so delete them here.
                    pIoContext = nullptr; // Context has been handled.
                    break;
                } // End of case IOOperationType::Send

                default:
                    // This case should ideally not be reached if `operationType` is always correctly set.
                    RF_NETWORK_ERROR("UDPSocketAsync: WorkerThread - Dequeued completed op with Unknown/None type. Context: %p, OpType: %d",
                        (void*)pIoContext, (pIoContext ? static_cast<int>(pIoContext->operationType) : -1));
                    if (m_eventHandler) m_eventHandler->OnNetworkError("Unknown operation type dequeued", (pIoContext ? static_cast<int>(pIoContext->operationType) : -1));
                    if (pIoContext) { // Attempt to clean up unexpected context types.
                        RF_NETWORK_ERROR("WorkerThread: Deleting unexpected context %p due to unknown type.", (void*)pIoContext);
                        delete pIoContext; // Assume dynamically allocated for safety.
                        pIoContext = nullptr;
                    }
                    break;
                } // End switch on operationType
            } // End while(m_isRunning) loop

            std::ostringstream exit_tid_oss;
            exit_tid_oss << std::this_thread::get_id();
            RF_NETWORK_INFO("UDPSocketAsync: Worker thread %s exiting gracefully.", exit_tid_oss.str().c_str());
        }

        // SendData: Sends raw data asynchronously to a specified recipient.
        bool UDPSocketAsync::SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) {
            if (m_socket == INVALID_SOCKET) {
                RF_NETWORK_ERROR("UDPSocketAsync::SendData: Socket not valid. Cannot send to %s.", recipient.ToString().c_str());
                return false;
            }
            // Allow 0-byte datagrams if your protocol needs them, otherwise make size > 0.
            if (size == 0 && data != nullptr) { // 0-byte datagrams are valid UDP, but check if data is null.
                RF_NETWORK_WARN("UDPSocketAsync::SendData: Attempting to send 0 bytes to %s. Proceeding.", recipient.ToString().c_str());
            }
            else if (data == nullptr && size > 0) { // Data pointer null but size > 0 is an error.
                RF_NETWORK_ERROR("UDPSocketAsync::SendData: Data is null but size %u > 0 for sending to %s.", size, recipient.ToString().c_str());
                return false;
            }

            // Create a new OverlappedIOContext for each send operation.
            // This context is dynamically allocated and will be deleted by the worker thread
            // when the send operation completes via `OnSendCompleted`.
            OverlappedIOContext* sendContext = nullptr;
            try {
                sendContext = new OverlappedIOContext(IOOperationType::Send, static_cast<size_t>(size));
            }
            catch (const std::bad_alloc& e) {
                RF_NETWORK_CRITICAL("UDPSocketAsync::SendData: Failed to allocate memory for send context to %s: %s", recipient.ToString().c_str(), e.what());
                return false;
            }

            // Copy the data into the context's buffer.
            if (size > 0 && data != nullptr) { // Only copy if there's data and a valid pointer.
                std::memcpy(sendContext->buffer.data(), data, size);
            }
            sendContext->wsaBuf.len = size; // Set the buffer length for WSASendTo.

            // Set up recipient address.
            sendContext->remoteAddrNative.sin_family = AF_INET;
            sendContext->remoteAddrNative.sin_port = htons(recipient.port);
            // Convert recipient IP string to binary.
            if (inet_pton(AF_INET, recipient.ipAddress.c_str(), &(sendContext->remoteAddrNative.sin_addr)) != 1) {
                RF_NETWORK_ERROR("UDPSocketAsync::SendData: inet_pton failed for IP %s to %s. Error: %d", recipient.ipAddress.c_str(), recipient.ToString().c_str(), WSAGetLastError());
                delete sendContext; // Clean up context on failure.
                return false;
            }
            sendContext->remoteAddrNativeLen = sizeof(sockaddr_in); // Set size of address structure.

            RF_NETWORK_TRACE("UDPSocketAsync::SendData: Attempting WSASendTo %u bytes to %s.", size, recipient.ToString().c_str());

            // Post the asynchronous send operation.
            DWORD bytesSent = 0; // Will be filled for immediate completion, or by GQCS for pending.
            int result = WSASendTo(m_socket,                  // The socket to send from.
                &(sendContext->wsaBuf),     // WSABUF structure.
                1,                          // Number of WSABUF structures.
                &bytesSent,                 // lpNumberOfBytesSent (for immediate completion).
                0,                          // dwFlags.
                (SOCKADDR*)&(sendContext->remoteAddrNative), // Destination address.
                sendContext->remoteAddrNativeLen,           // Size of destination address.
                &(sendContext->overlapped), // The OVERLAPPED structure.
                NULL);                      // lpCompletionRoutine (NULL for IOCP).

            if (result == SOCKET_ERROR) {
                int errorCode = WSAGetLastError();
                if (errorCode != WSA_IO_PENDING) { // If it failed immediately and is not pending.
                    RF_NETWORK_ERROR("UDPSocketAsync::SendData: WSASendTo failed immediately to %s with error: %d.", recipient.ToString().c_str(), errorCode);
                    // Notify handler of failed send attempt.
                    if (m_eventHandler) m_eventHandler->OnSendCompleted(sendContext, false, 0);
                    delete sendContext; // Clean up context on failure.
                    return false;
                }
                // If WSA_IO_PENDING, the operation will eventually complete via IOCP.
                RF_NETWORK_TRACE("UDPSocketAsync::SendData: WSASendTo pending for %s.", recipient.ToString().c_str());
            }
            else {
                // Operation completed immediately. A completion packet is still queued to the IOCP.
                RF_NETWORK_TRACE("UDPSocketAsync::SendData: WSASendTo completed immediately for %s.", recipient.ToString().c_str());
            }
            return true;
        }

    } // namespace Networking
} // namespace RiftForged