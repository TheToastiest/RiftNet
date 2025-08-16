#include "pch.h"
#include "RiftNetIO.hpp"
#include "../../../utilities/logger/Logger.hpp"
#include "../iocpmanager/IOCPManager.hpp"
#include <WS2tcpip.h> // For inet_pton

using namespace RiftNet::Logging;

#pragma comment(lib, "Ws2_32.lib")

namespace RiftNet::Networking {

    WinSocketIO::WinSocketIO() {
        // Initialize Winsock
        WSADATA wsaData;
        int result = WSAStartup(MAKEWORD(2, 2), &wsaData);
        if (result != 0) {
            // This is a catastrophic failure, throwing is appropriate.
            throw std::runtime_error("WSAStartup failed with error: " + std::to_string(result));
        }
    }

    WinSocketIO::~WinSocketIO() {
        Stop();
        // Cleanup Winsock
        WSACleanup();
    }

    bool WinSocketIO::Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) {
        if (m_socket != INVALID_SOCKET) {
            return true;
        }

        m_eventHandler = eventHandler;
        if (!m_eventHandler) {
            RF_NETWORK_CRITICAL("WinSocketIO cannot be initialized with a null event handler.");
            return false;
        }

        RF_NETWORK_INFO("Initializing WinSocketIO on {}:{}", listenIp, listenPort);

        m_socket = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (m_socket == INVALID_SOCKET) {
            RF_NETWORK_CRITICAL("Failed to create socket. Error: {}", WSAGetLastError());
            return false;
        }

        sockaddr_in localAddr{};
        localAddr.sin_family = AF_INET;
        localAddr.sin_port = htons(listenPort);
        inet_pton(AF_INET, listenIp.c_str(), &localAddr.sin_addr);

        if (bind(m_socket, (SOCKADDR*)&localAddr, sizeof(localAddr)) == SOCKET_ERROR) {
            RF_NETWORK_CRITICAL("Failed to bind socket to port {}. Error: {}", listenPort, WSAGetLastError());
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
            return false;
        }

        m_iocpManager = std::make_unique<IOCPManager>();
        if (!m_iocpManager->Start([this](auto... args) { this->OnIOCompleted(args...); })) {
            RF_NETWORK_CRITICAL("Failed to start IOCP Manager.");
            return false;
        }

        if (!m_iocpManager->AssociateSocket(m_socket)) {
            return false;
        }

        const int POOL_SIZE = 128;
        m_receiveContextPool.reserve(POOL_SIZE);
        m_freeReceiveContexts.reserve(POOL_SIZE);
        for (int i = 0; i < POOL_SIZE; ++i) {
            auto context = std::make_unique<OverlappedIOContext>(IOOperationType::Recv);
            m_freeReceiveContexts.push_back(context.get());
            m_receiveContextPool.push_back(std::move(context));
        }
        RF_NETWORK_DEBUG("Receive context pool initialized with {} contexts.", POOL_SIZE);

        return true;
    }

    bool WinSocketIO::Start() {
        if (m_isRunning) {
            return true;
        }
        m_isRunning = true;

        RF_NETWORK_INFO("WinSocketIO started. Posting initial receive requests.");

        int postedCount = 0;
        {
            std::lock_guard<std::mutex> lock(m_poolMutex);
            for (auto* context : m_freeReceiveContexts) {
                if (PostReceive(context)) {
                    postedCount++;
                }
            }
            m_freeReceiveContexts.clear();
        }
        RF_NETWORK_DEBUG("Posted {} initial receive requests.", postedCount);
        return postedCount > 0;
    }

    void WinSocketIO::Stop() {
        if (!m_isRunning.exchange(false)) {
            return;
        }

        RF_NETWORK_INFO("WinSocketIO stopping...");

        if (m_socket != INVALID_SOCKET) {
            closesocket(m_socket);
            m_socket = INVALID_SOCKET;
        }

        if (m_iocpManager) {
            m_iocpManager->Stop();
        }
    }

    bool WinSocketIO::SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) {
        if (!m_isRunning || data == nullptr) return false;

        auto sendContext = new OverlappedIOContext(IOOperationType::Send, size);
        memcpy(sendContext->buffer.data(), data, size);
        sendContext->wsaBuf.len = size;
        sendContext->endpoint = recipient;
        sendContext->remoteAddrNative = recipient.ToSockAddr();

        DWORD bytesSent = 0;
        int result = WSASendTo(
            m_socket, &sendContext->wsaBuf, 1, &bytesSent, 0,
            (SOCKADDR*)&sendContext->remoteAddrNative, sizeof(sendContext->remoteAddrNative),
            &sendContext->overlapped, NULL
        );

        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                RF_NETWORK_ERROR("WSASendTo to {} failed immediately. Error: {}", recipient.ToString(), error);
                delete sendContext;
                return false;
            }
        }

        RF_NETWORK_TRACE("Posted send of {} bytes to {}.", size, recipient.ToString());
        return true;
    }

    bool WinSocketIO::IsRunning() const {
        return m_isRunning;
    }

    void WinSocketIO::OnIOCompleted(OverlappedIOContext* context, DWORD bytesTransferred) {
        if (!m_isRunning) {
            if (context->operationType == IOOperationType::Send) delete context;
            return;
        }

        switch (context->operationType) {
        case IOOperationType::Recv: {
            if (bytesTransferred > 0) {
                context->endpoint = NetworkEndpoint(context->remoteAddrNative);
                RF_NETWORK_TRACE("Received {} bytes from {}.", bytesTransferred, context->endpoint.ToString());
                m_eventHandler->OnRawDataReceived(
                    context->endpoint, (const uint8_t*)context->buffer.data(), bytesTransferred, context
                );
            }
            // Always re-post the receive, even on 0-byte reads or errors, to keep listening.
            PostReceive(context);
            break;
        }
        case IOOperationType::Send: {
            RF_NETWORK_TRACE("Send to {} completed, success: {}, bytes: {}.", context->endpoint.ToString(), bytesTransferred > 0, bytesTransferred);
            m_eventHandler->OnSendCompleted(context, bytesTransferred > 0, bytesTransferred);
            delete context; // Send contexts are heap-allocated per-operation.
            break;
        }
        default:
            RF_NETWORK_WARN("Unhandled IOOperationType in OnIOCompleted.");
            break;
        }
    }

    bool WinSocketIO::PostReceive(OverlappedIOContext* context) {
        if (!m_isRunning) {
            ReturnReceiveContext(context);
            return false;
        }

        context->ResetForReceive();
        DWORD flags = 0;
        DWORD bytesReceived = 0;

        int result = WSARecvFrom(
            m_socket, &context->wsaBuf, 1, &bytesReceived, &flags,
            (SOCKADDR*)&context->remoteAddrNative, &context->remoteAddrNativeLen,
            &context->overlapped, NULL
        );

        if (result == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error != WSA_IO_PENDING) {
                if (m_isRunning) {
                    RF_NETWORK_ERROR("WSARecvFrom failed immediately. Error: {}", error);
                }
                ReturnReceiveContext(context);
                return false;
            }
        }
        return true;
    }

    OverlappedIOContext* WinSocketIO::GetFreeReceiveContext() {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        if (m_freeReceiveContexts.empty()) {
            RF_NETWORK_WARN("Receive context pool is empty. Allocating a new context.");
            auto new_context = std::make_unique<OverlappedIOContext>(IOOperationType::Recv);
            auto* ptr = new_context.get();
            m_receiveContextPool.push_back(std::move(new_context));
            return ptr;
        }
        OverlappedIOContext* context = m_freeReceiveContexts.back();
        m_freeReceiveContexts.pop_back();
        return context;
    }

    void WinSocketIO::ReturnReceiveContext(OverlappedIOContext* context) {
        std::lock_guard<std::mutex> lock(m_poolMutex);
        m_freeReceiveContexts.push_back(context);
    }

} // namespace RiftNet::Networking
