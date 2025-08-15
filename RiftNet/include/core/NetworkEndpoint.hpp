#pragma once
#include <string>
#include <cstdint> // For uint16_t

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Winsock2.h>  
#include <Ws2tcpip.h>  
#pragma comment(lib, "Ws2_32.lib") 


namespace RiftNet {
    namespace Networking {

        struct NetworkEndpoint {
            std::string ipAddress;
            uint16_t port;

            NetworkEndpoint(const std::string& ip = "", uint16_t p = 0)
                : ipAddress(ip), port(p) {
            }

            NetworkEndpoint(const sockaddr_in& addr) {
                char ipStr[INET_ADDRSTRLEN] = { 0 };
                inet_ntop(AF_INET, &(addr.sin_addr), ipStr, INET_ADDRSTRLEN);
                ipAddress = ipStr;
                port = ntohs(addr.sin_port);
            }

            std::string ToString() const {
                return ipAddress + ":" + std::to_string(port);
            }

            bool operator<(const NetworkEndpoint& other) const {
                if (ipAddress < other.ipAddress) {
                    return true;
                }
                if (ipAddress > other.ipAddress) {
                    return false;
                }
                return port < other.port;
            }

            sockaddr_in ToSockAddr() const {
                sockaddr_in addr = {};
                addr.sin_family = AF_INET;
                addr.sin_port = htons(port);
                inet_pton(AF_INET, ipAddress.c_str(), &addr.sin_addr);
                return addr;
            }

            bool operator==(const NetworkEndpoint& other) const {
                return ipAddress == other.ipAddress && port == other.port;
            }
        };

    } // namespace Networking
} // namespace RiftForged
