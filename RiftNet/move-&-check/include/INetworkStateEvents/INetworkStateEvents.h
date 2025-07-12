#pragma once
#include "../NetworkEndpoint/NetworkEndpoint.h"

namespace RiftForged {
    namespace Networking {

        /**
         * @class INetworkStateEvents
         * @brief Defines the callback interface for the Network library to report
         * state changes to a higher-level module without depending on it.
         */
        class INetworkStateEvents {
        public:
            virtual ~INetworkStateEvents() = default;

            /**
             * @brief Called by the network session layer (e.g., UDPPacketHandler)
             * when it determines a client connection has timed out due to inactivity.
             * @param client The network endpoint of the timed-out client.
             */
            virtual void OnClientTimedOut(const NetworkEndpoint& client) = 0;
        };

    } // namespace Networking
} // namespace RiftForged