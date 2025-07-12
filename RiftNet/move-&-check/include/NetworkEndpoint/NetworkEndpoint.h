#pragma once
#include <string>
#include <cstdint> // For uint16_t

// Forward declare a system-specific address structure if needed, or use general fields
// For Winsock, this would eventually wrap something like sockaddr_in.
// For simplicity here, we'll store IP as string and port.

namespace RiftForged {
    namespace Networking {

        struct NetworkEndpoint {
            std::string ipAddress;
            uint16_t port;
            // You might also store a unique ID for this endpoint if assigned by ClientManager

            NetworkEndpoint(const std::string& ip = "", uint16_t p = 0)
                : ipAddress(ip), port(p) {
            }

            // For logging or map keys
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
                // IP addresses are equal, compare ports
                return port < other.port;
            }

            // Equality for comparisons, map keys etc.
            bool operator==(const NetworkEndpoint& other) const {
                return ipAddress == other.ipAddress && port == other.port;
            }
            // Add operator< if used as a key in std::map without a custom comparator
        };

    } // namespace Networking
} // namespace RiftForged