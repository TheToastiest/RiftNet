#include "pch.h"
#include "IOCPManager.hpp"
#include "../../../utilities/logger/Logger.hpp"

using namespace RiftNet::Logging;

namespace RiftNet::Networking {

    // Helper function to determine the optimal number of worker threads.
    static unsigned int DetermineNumWorkerThreads(uint32_t concurrentThreads) {
        if (concurrentThreads > 0) {
            return concurrentThreads;
        }
        unsigned int num_threads = std::thread::hardware_concurrency();
        return (num_threads > 0) ? num_threads : 4; // Default to 4 if hardware_concurrency is not available.
    }

    IOCPManager::IOCPManager() = default;

    IOCPManager::~IOCPManager() {
        Stop();
    }

    bool IOCPManager::Start(OnIOCompletedCallback callback, uint32_t concurrentThreads) {
        if (m_isRunning) {
            return true;
        }

        m_ioCompletedCallback = callback;

        // Create the I/O Completion Port. The last parameter (NumberOfConcurrentThreads) is a hint to the OS.
        m_iocpHandle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, concurrentThreads);
        if (m_iocpHandle == NULL) {
            RF_NETWORK_CRITICAL("Failed to create IOCP handle. Error: {}", GetLastError());
            return false;
        }

        m_isRunning = true;

        unsigned int threadCount = DetermineNumWorkerThreads(concurrentThreads);
        RF_NETWORK_INFO("IOCPManager starting with {} worker threads.", threadCount);

        try {
            m_workerThreads.reserve(threadCount);
            for (unsigned int i = 0; i < threadCount; ++i) {
                m_workerThreads.emplace_back(&IOCPManager::WorkerThread, this);
            }
        }
        catch (const std::system_error& e) {
            RF_NETWORK_CRITICAL("Failed to create worker thread: {}", e.what());
            Stop();
            return false;
        }

        return true;
    }

    void IOCPManager::Stop() {
        if (!m_isRunning.exchange(false)) {
            return;
        }

        RF_NETWORK_INFO("IOCPManager stopping...");

        if (m_iocpHandle != NULL) {
            for (size_t i = 0; i < m_workerThreads.size(); ++i) {
                // Post a "poison pill" packet to wake up each thread and signal it to exit.
                PostQueuedCompletionStatus(m_iocpHandle, 0, 0, NULL);
            }
        }

        for (auto& thread : m_workerThreads) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        m_workerThreads.clear();
        RF_NETWORK_DEBUG("All IOCP worker threads have stopped.");

        if (m_iocpHandle != NULL) {
            CloseHandle(m_iocpHandle);
            m_iocpHandle = NULL;
        }
    }

    bool IOCPManager::AssociateSocket(SOCKET socket) {
        if (m_iocpHandle == NULL) {
            RF_NETWORK_ERROR("Cannot associate socket, IOCP handle is invalid.");
            return false;
        }

        // Associate the socket with the IOCP. The completion key can be used to pass
        // per-socket data, but for a single-socket server, it's less critical.
        if (CreateIoCompletionPort((HANDLE)socket, m_iocpHandle, (ULONG_PTR)0, 0) == NULL) {
            RF_NETWORK_ERROR("Failed to associate socket with IOCP. Error: {}", GetLastError());
            return false;
        }

        RF_NETWORK_DEBUG("Successfully associated socket {} with IOCP.", socket);
        return true;
    }

    void IOCPManager::WorkerThread() {
        std::stringstream ss_start;
        ss_start << std::this_thread::get_id();
        RF_NETWORK_DEBUG("IOCP worker thread {} starting.", ss_start.str());

        DWORD bytesTransferred = 0;
        ULONG_PTR completionKey = 0;
        OverlappedIOContext* pIoContext = nullptr;

        while (m_isRunning.load(std::memory_order_acquire)) {
            BOOL success = GetQueuedCompletionStatus(
                m_iocpHandle,
                &bytesTransferred,
                &completionKey,
                (LPOVERLAPPED*)&pIoContext,
                100 // Timeout in ms to allow periodic checks of m_isRunning
            );

            if (!success) {
                DWORD errorCode = GetLastError();
                if (errorCode == WAIT_TIMEOUT) {
                    continue; // Expected timeout, loop again.
                }

                // An actual error occurred.
                if (pIoContext != nullptr) {
                    // An I/O operation itself failed. The owner needs to handle this.
                    m_ioCompletedCallback(pIoContext, 0); // 0 bytes transferred signifies failure
                }
                else {
                    // A critical error with GQCS itself, likely during shutdown.
                    RF_NETWORK_WARN("GetQueuedCompletionStatus failed without a context. Error: {}. Assuming shutdown.", errorCode);
                }
                continue;
            }

            // This is the explicit shutdown signal from Stop().
            if (pIoContext == NULL) {
                break;
            }

            // A valid operation completed.
            if (m_ioCompletedCallback) {
                m_ioCompletedCallback(pIoContext, bytesTransferred);
            }
        }

        std::stringstream ss_exit;
        ss_exit << std::this_thread::get_id();
        RF_NETWORK_DEBUG("IOCP worker thread {} exiting.", ss_exit.str());
    }

} // namespace RiftNet::Networking
