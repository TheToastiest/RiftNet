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

            virtual bool Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) = 0;


            virtual bool Start() = 0;


            virtual void Stop() = 0;


            virtual bool SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) = 0;


            virtual bool IsRunning() const = 0;


            //virtual OverlappedIOContext* GetFreeReceiveContext() = 0;
            //virtual void ReturnReceiveContext(OverlappedIOContext* pContext) = 0;
            //virtual bool PostReceive(OverlappedIOContext* pRecvContext) = 0;
            
        };

    } // namespace Networking
} // namespace RiftForged
