#pragma once

#include "../../protocol/UDPReliabilityProtocol/UDPReliabilityProtocol.hpp"
#include "../networkio/NetworkEndpoint.hpp"
#include "../../security/crypto/Encryptor.hpp"
#include "../../compression/compressor/Compressor.hpp"

#include <functional>
#include <memory>
#include <vector>
#include <deque>
#include <chrono>
#include <atomic>
#include <mutex>
#include <cstdint>

namespace RiftNet::Protocol {

    /**
     * @class Connection
     * @brief Manages the state and data processing pipeline for a single remote peer.
     *        Encapsulates reliability, security, compression, and a minimal cleartext handshake.
     */
    class Connection {
    public:
        using SendCallback = std::function<void(const RiftNet::Networking::NetworkEndpoint&, const std::vector<uint8_t>&)>;
        using AppDataCallback = std::function<void(const uint8_t*, uint32_t)>;

        explicit Connection(const RiftNet::Networking::NetworkEndpoint& endpoint, bool isServer);

        // --- Configuration ---
        void SetSendCallback(SendCallback cb);
        void SetAppDataCallback(AppDataCallback cb);

        // --- Handshake / Session setup ---
        void BeginHandshake();                         // safe to call multiple times
        bool InitializeSession(const byte_vec& remotePublicKey);
        const byte_vec& GetPublicKey() const;

        // --- Main Pipeline Methods ---
        void ProcessIncomingRawPacket(const uint8_t* data, uint32_t size);
        void SendApplicationData(const uint8_t* data, uint32_t size, bool isReliable);
        void Update(std::chrono::steady_clock::time_point now);

        // --- State Queries ---
        bool IsTimedOut(std::chrono::steady_clock::time_point now, std::chrono::seconds timeout) const;
        bool IsSecure() const;
        const RiftNet::Networking::NetworkEndpoint& GetEndpoint() const;

    private:
        // --- Private Pipeline Methods ---
        void HandleDecryptedPacket(const uint8_t* data, uint32_t size);
        void SendPacket(const std::vector<uint8_t>& packet_data);
        bool MaybeHandleCleartextHandshake(const uint8_t* data, uint32_t size);

        // Flush any queued app payloads now that the channel is secure
        void FlushPendingSends();

        // --- Connection State ---
        RiftNet::Networking::NetworkEndpoint m_endpoint;
        ReliableConnectionState m_reliabilityState;
        std::unique_ptr<RiftNet::Security::Encryptor>     m_encryptor;
        std::unique_ptr<RiftNet::Compression::Compressor> m_compressor;

        // Nonce counters for encryption (Client even, Server odd)
        std::atomic<uint64_t> m_txNonce{ 0 };
        std::atomic<uint64_t> m_rxNonce{ 0 }; // last seen rx (diagnostic)

        // Cleartext handshake state
        std::atomic<bool> m_handshakeStarted{ false };

        // --- Callbacks ---
        SendCallback    m_sendCallback;
        AppDataCallback m_appDataCallback;

        // --- Pre-secure send queue ---
        struct PendingSend { std::vector<uint8_t> data; bool reliable; };
        std::mutex              m_pendingMtx;
        std::deque<PendingSend> m_pendingSends;
        size_t                  m_pendingBytes{ 0 };
        static constexpr size_t kMaxPendingBytes = 512 * 1024; // backpressure
    };

} // namespace RiftNet::Protocol
