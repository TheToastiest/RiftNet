// core/IConnectionManager.hpp
#pragma once
#include <vector>

namespace RiftForged::Networking {
    class IConnectionManager {
    public:
        virtual ~IConnectionManager() = default;

        // Send raw data to a specific connection by ID
        virtual bool SendToConnection(uint64_t connectionId, const std::vector<uint8_t>& data) = 0;
    };
}
