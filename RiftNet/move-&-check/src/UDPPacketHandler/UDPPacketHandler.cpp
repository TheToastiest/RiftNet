#include "../../include/UDPPacketHandler/UDPPacketHandler.h"
#include "../../include/INetworkIO/INetworkIO.h"
#include "../../include/IApplicationPayloadHandler/IApplicationPayloadHandler.h"
#include "../../include/INetworkStateEvents/INetworkStateEvents.h"
#include "../../include/UDPReliabilityProtocol/UDPReliabilityProtocol.h"
#include "../../include/ReliableConnectionState/ReliableConnectionState.h"
#include "../../include/GamePacketHeader/GamePacketHeader.h"
#include "../../../Core/Utilities/include/Logger/Logger.h"
#include <chrono>
#include <stdexcept>

const int RELIABILITY_THREAD_SLEEP_MS = 20;
const int STALE_CONNECTION_TIMEOUT_SECONDS = 60;

namespace RiftForged {
    namespace Networking {

        UDPPacketHandler::UDPPacketHandler(INetworkIO* networkIO, IApplicationPayloadHandler* payloadHandler, INetworkStateEvents* stateEvents)
            : m_networkIO(networkIO),
            m_payloadHandler(payloadHandler),
            m_stateEvents(stateEvents),
            m_isRunning(false)
        {
            if (!m_networkIO || !m_payloadHandler || !m_stateEvents) {
                throw std::invalid_argument("UDPPacketHandler: All interface pointers must be valid.");
            }
        }

        UDPPacketHandler::~UDPPacketHandler() {
            Stop();
        }

        void UDPPacketHandler::Start() {
            if (m_isRunning.exchange(true)) return;

            try {
                m_reliabilityThread = std::thread(&UDPPacketHandler::ReliabilityManagementThread, this);
            }
            catch (const std::system_error& e) {
                RF_NETWORK_CRITICAL("Failed to create reliability thread: {}", e.what());
                m_isRunning = false;
            }
        }

        void UDPPacketHandler::Stop() {
            if (!m_isRunning.exchange(false)) return;

            if (m_reliabilityThread.joinable()) {
                m_reliabilityThread.join();
            }
        }

        void UDPPacketHandler::OnRawDataReceived(const NetworkEndpoint& sender, const uint8_t* data, uint32_t size, OverlappedIOContext* context) {
            if (!m_isRunning || size < GetGamePacketHeaderSize()) {
                return;
            }

            GamePacketHeader receivedHeader;
            memcpy(&receivedHeader, data, GetGamePacketHeaderSize());

            auto connState = GetOrCreateReliabilityState(sender);
            if (!connState) return;

            std::vector<uint8_t> reassembledPayload;
            const uint8_t* payloadPtr = data + GetGamePacketHeaderSize();
            uint16_t payloadSize = static_cast<uint16_t>(size - GetGamePacketHeaderSize()); // This cast was already correct

            bool shouldProcessPayload = UDPReliabilityProtocol::ProcessIncomingHeader(
                *connState,
                receivedHeader,
                payloadPtr,
                payloadSize,
                reassembledPayload
            );

            if (shouldProcessPayload && !reassembledPayload.empty()) {
                // FIX: Explicitly cast size_t to uint32_t
                m_payloadHandler->ProcessPayload(sender, reassembledPayload.data(), static_cast<uint32_t>(reassembledPayload.size()));
            }

            if (UDPReliabilityProtocol::ShouldSendAck(*connState)) {
                SendAckPacket(sender, *connState);
            }
        }

        bool UDPPacketHandler::SendReliablePacket(const NetworkEndpoint& recipient, const uint8_t* payload, uint32_t payloadSize) {
            if (!m_isRunning) return false;
            auto connState = GetOrCreateReliabilityState(recipient);
            if (!connState) return false;

            auto packetsToSend = UDPReliabilityProtocol::PrepareOutgoingPackets(
                *connState, payload, payloadSize, static_cast<uint8_t>(GamePacketFlag::IS_RELIABLE));

            bool allSent = true;
            for (const auto& packet : packetsToSend) {
                // FIX: Explicitly cast size_t to uint32_t
                if (!m_networkIO->SendData(recipient, packet.data(), static_cast<uint32_t>(packet.size()))) {
                    allSent = false;
                }
            }
            return allSent;
        }

        bool UDPPacketHandler::SendUnreliablePacket(const NetworkEndpoint& recipient, const uint8_t* payload, uint32_t payloadSize) {
            auto connState = GetOrCreateReliabilityState(recipient);
            if (!connState) return false;

            auto packetsToSend = UDPReliabilityProtocol::PrepareOutgoingPackets(
                *connState, payload, payloadSize, static_cast<uint8_t>(GamePacketFlag::NONE));

            bool allSent = true;
            for (const auto& packet : packetsToSend) {
                // FIX: Explicitly cast size_t to uint32_t
                if (!m_networkIO->SendData(recipient, packet.data(), static_cast<uint32_t>(packet.size()))) {
                    allSent = false;
                }
            }
            return allSent;
        }

        bool UDPPacketHandler::SendAckPacket(const NetworkEndpoint& recipient, ReliableConnectionState& connectionState) {
            auto packetsToSend = UDPReliabilityProtocol::PrepareOutgoingPackets(
                connectionState,
                nullptr,
                0,
                static_cast<uint8_t>(GamePacketFlag::IS_ACK_ONLY)
            );

            if (packetsToSend.empty() || packetsToSend[0].empty()) return false;

            // FIX: Explicitly cast size_t to uint32_t
            return m_networkIO->SendData(recipient, packetsToSend[0].data(), static_cast<uint32_t>(packetsToSend[0].size()));
        }

        void UDPPacketHandler::OnSendCompleted(OverlappedIOContext* context, bool success, uint32_t bytesSent) {
            // This can be used for logging or metrics in the future.
        }

        void UDPPacketHandler::OnNetworkError(const std::string& errorMessage, int errorCode) {
            RF_NETWORK_ERROR("A network error was reported by the transport layer: {} (Code: {})", errorMessage, errorCode);
        }

        void UDPPacketHandler::ReliabilityManagementThread() {
            while (m_isRunning) {
                std::vector<NetworkEndpoint> timedOutClients;

                { // Lock scope begins
                    std::lock_guard<std::mutex> lock(m_reliabilityStatesMutex);
                    auto now = std::chrono::steady_clock::now();

                    for (auto it = m_reliabilityStates.begin(); it != m_reliabilityStates.end();) {
                        auto& endpoint = it->first;
                        auto& connState = it->second;

                        UDPReliabilityProtocol::ProcessRetransmissions(*connState, [this, &endpoint](const std::vector<uint8_t>& packet) {
                            // FIX: Explicitly cast size_t to uint32_t
                            m_networkIO->SendData(endpoint, packet.data(), static_cast<uint32_t>(packet.size()));
                            });

                        if (UDPReliabilityProtocol::IsConnectionTimedOut(*connState, now, STALE_CONNECTION_TIMEOUT_SECONDS)) {
                            timedOutClients.push_back(endpoint);
                            it = m_reliabilityStates.erase(it);
                        }
                        else {
                            ++it;
                        }
                    }
                } // Lock scope ends

                for (const auto& client : timedOutClients) {
                    RF_NETWORK_INFO("Client {} timed out.", client.ToString());
                    m_stateEvents->OnClientTimedOut(client);
                }

                std::this_thread::sleep_for(std::chrono::milliseconds(RELIABILITY_THREAD_SLEEP_MS));
            }
        }

        std::shared_ptr<ReliableConnectionState> UDPPacketHandler::GetOrCreateReliabilityState(const NetworkEndpoint& endpoint) {
            std::lock_guard<std::mutex> lock(m_reliabilityStatesMutex);
            auto it = m_reliabilityStates.find(endpoint);
            if (it == m_reliabilityStates.end()) {
                auto newState = std::make_shared<ReliableConnectionState>();
                m_reliabilityStates[endpoint] = newState;
                return newState;
            }
            return it->second;
        }

    } // namespace Networking
} // namespace RiftForged