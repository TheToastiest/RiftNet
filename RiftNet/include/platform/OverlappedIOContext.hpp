// File: OverlappedIOContext.h
#pragma once

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Winsock2.h> 
#include <vector>   
#include <cstring>   
#include "../core/NetworkEndpoint.hpp"


const int DEFAULT_IOCP_UDP_BUFFER_SIZE = 4096; 

namespace RiftForged {
    namespace Networking {

        enum class IOOperationType {
            None,
            Recv,
            Send
        };

        struct OverlappedIOContext {
            OVERLAPPED      overlapped;
            IOOperationType operationType;
            WSABUF          wsaBuf;
            std::vector<char> buffer; 
            sockaddr_in     remoteAddrNative;
            int             remoteAddrNativeLen;
            NetworkEndpoint endpoint;

            OverlappedIOContext(IOOperationType opType, size_t bufferSize = DEFAULT_IOCP_UDP_BUFFER_SIZE)
                : operationType(opType), buffer(bufferSize), remoteAddrNativeLen(sizeof(sockaddr_in)) {
                ZeroMemory(&overlapped, sizeof(OVERLAPPED));
                ZeroMemory(&remoteAddrNative, sizeof(sockaddr_in));
                wsaBuf.buf = buffer.data();
                wsaBuf.len = static_cast<ULONG>(buffer.size());
            }

            void ResetForReceive() {
                ZeroMemory(&overlapped, sizeof(OVERLAPPED));
                ZeroMemory(&remoteAddrNative, sizeof(sockaddr_in));
                operationType = IOOperationType::Recv;
                remoteAddrNativeLen = sizeof(sockaddr_in);
                wsaBuf.buf = buffer.data();
                wsaBuf.len = static_cast<ULONG>(buffer.size());
            }
        };

    } // namespace Networking
} // namespace RiftForged
