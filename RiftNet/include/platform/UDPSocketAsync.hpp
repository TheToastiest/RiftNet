// File: UDPSocketAsync.h
// RiftForged Game Engine
// Copyright (C) 2023 RiftForged Team
// Description: Header file for the UDPSocketAsync class, which handles asynchronous UDP socket operations using IOCP.
// This class will implement the INetworkIO interface.

#pragma once

#include <string>           
#include <vector>          
#include <thread>          
#include <atomic>        
#include <mutex>          
#include <deque>          
#include <memory>        

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX           
#endif
#include <Winsock2.h>       
#include <Ws2tcpip.h>      
#pragma comment(lib, "Ws2_32.lib") 

#include "../core/INetworkIO.hpp"         
#include "../core/NetworkEndpoint.hpp"   
#include "OverlappedIOContext.hpp"


const int DEFAULT_UDP_BUFFER_SIZE_IOCP = 4096; 
const int MAX_PENDING_RECEIVES_IOCP = 200;    

namespace RiftNet {
    namespace Networking {

        
        class UDPSocketAsync : public INetworkIO {
        public:
        
            UDPSocketAsync();

            ~UDPSocketAsync() override; 

            UDPSocketAsync(const UDPSocketAsync&) = delete;
            UDPSocketAsync& operator=(const UDPSocketAsync&) = delete;

           
            bool Init(const std::string& listenIp, uint16_t listenPort, INetworkIOEvents* eventHandler) override;


            bool Start() override;

            void Stop() override;

            bool SendData(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) override;

            bool SendTo(const NetworkEndpoint& recipient, const uint8_t* data, uint32_t size) {
                return SendData(recipient, data, size);
            }

            bool IsRunning() const override;

        private:
            void WorkerThread();

            bool PostReceiveInternal(OverlappedIOContext* pRecvContext);

            OverlappedIOContext* GetFreeReceiveContextInternal();

            void ReturnReceiveContextInternal(OverlappedIOContext* pContext);

            std::string m_listenIp;           
            uint16_t m_listenPort;           
            INetworkIOEvents* m_eventHandler; 

            SOCKET m_socket;                  
            HANDLE m_iocpHandle;         

            std::vector<std::thread> m_workerThreads;
            std::atomic<bool> m_isRunning;        

            std::vector<std::unique_ptr<OverlappedIOContext>> m_receiveContextPool; 
            std::deque<OverlappedIOContext*> m_freeReceiveContexts;             
            std::mutex m_receiveContextMutex;                                    
        };

    } // namespace Networking
} // namespace RiftForged
