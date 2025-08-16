#pragma once

#include "../riftnetio/RiftNetIO.hpp"
#include <Windows.h>
#include <vector>
#include <thread>
#include <functional>
#include <atomic>

namespace RiftNet {
    namespace Networking {

        /**
         * @class IOCPManager
         * @brief Manages the I/O Completion Port, worker threads, and dispatching of completed I/O events.
         * This is an internal, low-level component.
         */
        class IOCPManager {
        public:
            // Callback to notify the owner of a completed I/O operation.
            using OnIOCompletedCallback = std::function<void(OverlappedIOContext*, DWORD)>;

            IOCPManager();
            ~IOCPManager();

            // Non-copyable and non-movable
            IOCPManager(const IOCPManager&) = delete;
            IOCPManager& operator=(const IOCPManager&) = delete;

            /**
             * @brief Starts the IOCP worker threads.
             * @param callback The function to call when an I/O operation completes.
             * @param concurrentThreads The number of threads to run. 0 means system default (number of cores).
             * @return True on success, false on failure.
             */
            bool Start(OnIOCompletedCallback callback, uint32_t concurrentThreads = 0);

            /**
             * @brief Stops all worker threads and cleans up resources.
             */
            void Stop();

            /**
             * @brief Associates a socket with the I/O completion port.
             * @param socket The socket to associate.
             * @return True on success, false on failure.
             */
            bool AssociateSocket(SOCKET socket);

        private:
            /**
             * @brief The main function for each worker thread.
             */
            void WorkerThread();

            HANDLE m_iocpHandle = INVALID_HANDLE_VALUE;
            std::vector<std::thread> m_workerThreads;
            std::atomic<bool> m_isRunning = false;
            OnIOCompletedCallback m_ioCompletedCallback;
        };

    } // namespace Networking

} // namespace RiftNet