#pragma once

#include "../../include/INetworkIOEvents/INetworkIOEvents.h" // The interface we implement

#include <string>
#include <vector>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <atomic>
#include <chrono>

// Forward declare all dependencies from within the Network library
namespace RiftForged {
    namespace Networking {
        class INetworkIO;
        class IApplicationPayloadHandler;
        class INetworkStateEvents;
        struct ReliableConnectionState;
        struct NetworkEndpoint;
        struct OverlappedIOContext;
    }
}

namespace RiftForged {
    namespace Networking {

        /**
         * @class UDPPacketHandler
         * @brief Acts as the Session Layer for the network stack.
         * * This class listens for raw data events from an INetworkIO implementation (e.g., UDPSocketAsync).
         * It is responsible for handling the reliability protocol (acks, sequence numbers, retransmissions)
         * and managing connection state. It unwraps incoming packets to extract the application payload,
         * and wraps outgoing payloads with the necessary headers.
         *
         * It is completely agnostic to the content of the application payload.
         */
        class UDPPacketHandler : public INetworkIOEvents {
        public:
            /**
             * @brief Constructs the UDPPacketHandler.
             * @param networkIO A pointer to the underlying transport layer used to send raw data.
             * @param payloadHandler A pointer to the handler that will process application-level payloads.
             * @param stateEvents A pointer to the handler that will process network state events like timeouts.
             */
            UDPPacketHandler(INetworkIO* networkIO,
                IApplicationPayloadHandler* payloadHandler,
                INetworkStateEvents* stateEvents);

            ~UDPPacketHandler() override;

            // Disable copy and assignment
            UDPPacketHandler(const UDPPacketHandler&) = delete;
            UDPPacketHandler& operator=(const UDPPacketHandler&) = delete;

            /**
             * @brief Starts the reliability management thread.
             */
            void Start();

            /**
             * @brief Stops and joins the reliability management thread.
             */
            void Stop();

            // --- Public Sending Interface ---

            /**
             * @brief Wraps a payload with reliability headers and sends it.
             * @param recipient The target network endpoint.
             * @param payload Pointer to the raw application data to send.
             * @param payloadSize The size of the payload in bytes.
             * @return True if the send was successfully initiated, false otherwise.
             */
            bool SendReliablePacket(const NetworkEndpoint& recipient, const uint8_t* payload, uint32_t payloadSize);

            /**
             * @brief Wraps a payload with basic headers and sends it unreliably.
             * @param recipient The target network endpoint.
             * @param payload Pointer to the raw application data to send.
             * @param payloadSize The size of the payload in bytes.
             * @return True if the send was successfully initiated, false otherwise.
             */
            bool SendUnreliablePacket(const NetworkEndpoint& recipient, const uint8_t* payload, uint32_t payloadSize);

            // --- INetworkIOEvents Implementation ---
            void OnRawDataReceived(const NetworkEndpoint& sender, const uint8_t* data, uint32_t size, OverlappedIOContext* context) override;
            void OnSendCompleted(OverlappedIOContext* context, bool success, uint32_t bytesSent) override;
            void OnNetworkError(const std::string& errorMessage, int errorCode) override;

        private:
            void ReliabilityManagementThread();
            std::shared_ptr<ReliableConnectionState> GetOrCreateReliabilityState(const NetworkEndpoint& endpoint);
            bool SendAckPacket(const NetworkEndpoint& recipient, ReliableConnectionState& connectionState);

            // --- Member Variables ---
            INetworkIO* m_networkIO;
            IApplicationPayloadHandler* m_payloadHandler;
            INetworkStateEvents* m_stateEvents;

            std::atomic<bool> m_isRunning;
            std::thread m_reliabilityThread;

            std::mutex m_reliabilityStatesMutex;
            std::map<NetworkEndpoint, std::shared_ptr<ReliableConnectionState>> m_reliabilityStates;
        };

    } // namespace Networking
} // namespace RiftForged