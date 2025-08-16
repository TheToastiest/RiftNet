#pragma once

#include "../networkio/INetworkIO.hpp"
#include "../networkio/INetworkIOEvents.hpp"
// Removed the include for IOCPManager.hpp from the header

#include <memory>
#include <vector>
#include <mutex>

namespace RiftNet::Networking {

    // Forward declaration: Tells the compiler this class exists without needing its full definition.
    class IOCPManager;

    /**
     * @class WinSocketIO / RiftNetIO
     * @brief A Windows-specific implementation of the INetworkIO interface using IOCP.
     */
    class WinSocketIO : public INetworkIO { // Or class RiftNetIO if you renamed it
    public:
        WinSocketIO();
        virtual ~WinSocketIO() override;

        // --- INetworkIO Interface Implementation ---
        bool Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) override;
        bool Start() override;
        void Stop() override;
        bool SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) override;
        bool IsRunning() const override;

    private:
        /**
         * @brief The callback function passed to the IOCPManager.
         * This is where completed I/O operations land.
         */
        void OnIOCompleted(OverlappedIOContext* context, DWORD bytesTransferred);

        /**
         * @brief Posts a receive request to the IOCP.
         */
        bool PostReceive(OverlappedIOContext* context);

        /**
         * @brief Manages the pool of OverlappedIOContext objects for receiving data.
         */
        OverlappedIOContext* GetFreeReceiveContext();
        void ReturnReceiveContext(OverlappedIOContext* context);

        SOCKET m_socket = INVALID_SOCKET;
        INetworkIOEvents* m_eventHandler = nullptr;

        // This now works because of the forward declaration above.
        std::unique_ptr<IOCPManager> m_iocpManager;

        std::atomic<bool> m_isRunning = false;

        // Context pooling for receive operations to avoid frequent allocations
        std::vector<std::unique_ptr<OverlappedIOContext>> m_receiveContextPool;
        std::vector<OverlappedIOContext*> m_freeReceiveContexts;
        std::mutex m_poolMutex;
    };

} // namespace RiftNet::Networking
