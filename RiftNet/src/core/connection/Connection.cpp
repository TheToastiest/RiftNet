#include "pch.h"
#include "Connection.hpp"

#include "../../protocol/PacketFactory/PacketFactory.hpp"
#include "../../security/handshake/Handshake.hpp"
#include "../../../utilities/logger/Logger.hpp" // adjust include path if needed

#include <vector>
#include <string>
#include <cstring>
#include <array>
#include <cstdint>

namespace {
    // Endian helpers without pulling in socket headers
#if defined(_WIN32)
#include <intrin.h>
    inline uint64_t host_to_be64(uint64_t x) noexcept { return static_cast<uint64_t>(_byteswap_uint64(x)); }
    inline uint64_t be64_to_host(uint64_t x) noexcept { return static_cast<uint64_t>(_byteswap_uint64(x)); }
#else
#include <arpa/inet.h>
    inline uint64_t host_to_be64(uint64_t x) noexcept {
        uint32_t lo = static_cast<uint32_t>(x & 0xFFFFFFFFull);
        uint32_t hi = static_cast<uint32_t>(x >> 32);
        lo = htonl(lo); hi = htonl(hi);
        return (static_cast<uint64_t>(lo) << 32) | hi;
    }
    inline uint64_t be64_to_host(uint64_t x) noexcept {
        uint32_t lo = static_cast<uint32_t>(x & 0xFFFFFFFFull);
        uint32_t hi = static_cast<uint32_t>(x >> 32);
        lo = ntohl(lo); hi = ntohl(hi);
        return (static_cast<uint64_t>(lo) << 32) | hi;
    }
#endif
} // namespace

namespace RiftNet::Protocol {

    Connection::Connection(const RiftNet::Networking::NetworkEndpoint& endpoint, bool isServer)
        : m_endpoint(endpoint)
    {
        try {
            RF_NETWORK_DEBUG("Connection ctor: endpoint={}:{} isServer={}",
                endpoint.ipAddress, endpoint.port, isServer ? "true" : "false");

            m_encryptor = std::make_unique<RiftNet::Security::Encryptor>(isServer);
            m_compressor = std::make_unique<RiftNet::Compression::Compressor>();

            // Nonce policy: Client even, Server odd
            if (isServer) {
                m_txNonce.store(1, std::memory_order_relaxed);
                m_rxNonce.store(0, std::memory_order_relaxed);
            }
            else {
                m_txNonce.store(0, std::memory_order_relaxed);
                m_rxNonce.store(1, std::memory_order_relaxed);
            }

            RF_NETWORK_DEBUG("Connection ctor complete: txNonce={} rxNonce={}",
                m_txNonce.load(std::memory_order_relaxed),
                m_rxNonce.load(std::memory_order_relaxed));
        }
        catch (const std::exception& e) {
            RF_NETWORK_CRITICAL("Exception in Connection ctor: {}", e.what());
            throw;
        }
        catch (...) {
            RF_NETWORK_CRITICAL("Unknown exception in Connection ctor");
            throw;
        }
    }

    void Connection::SetSendCallback(SendCallback cb) { m_sendCallback = cb; }
    void Connection::SetAppDataCallback(AppDataCallback cb) { m_appDataCallback = cb; }

    bool Connection::InitializeSession(const byte_vec& remotePublicKey) {
        try {
            RF_NETWORK_DEBUG("InitializeSession: remotePublicKey size={}", remotePublicKey.size());
            const bool ok = m_encryptor->InitializeSession(remotePublicKey);
            RF_NETWORK_INFO("InitializeSession: {}", ok ? "success" : "failure");
            if (ok) {
                FlushPendingSends();
            }
            return ok;
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("InitializeSession failed: {}", e.what());
            return false;
        }
        catch (...) {
            RF_NETWORK_ERROR("InitializeSession failed: unknown exception");
            return false;
        }
    }

    const byte_vec& Connection::GetPublicKey() const { return m_encryptor->GetPublicKey(); }

    // ---------------- Handshake helpers ----------------

    void Connection::BeginHandshake() {
        if (m_handshakeStarted.exchange(true, std::memory_order_acq_rel)) {
            RF_NETWORK_TRACE("BeginHandshake: already started");
            return;
        }
        if (!m_sendCallback) {
            RF_NETWORK_WARN("BeginHandshake: send callback not set; cannot send HELLO");
            return;
        }

        const auto& pub = m_encryptor->GetPublicKey();
        if (pub.size() != 32) {
            RF_NETWORK_ERROR("BeginHandshake: local public key invalid size={}", pub.size());
            return;
        }

        auto hello = Handshake::BuildHello(pub);
        if (hello.empty()) {
            RF_NETWORK_ERROR("BeginHandshake: BuildHello failed");
            return;
        }

        RF_NETWORK_DEBUG("BeginHandshake: sending HELLO ({} bytes) to {}:{}",
            hello.size(), m_endpoint.ipAddress, m_endpoint.port);
        m_sendCallback(m_endpoint, hello); // plaintext
    }

    bool Connection::MaybeHandleCleartextHandshake(const uint8_t* data, uint32_t size) {
        byte_vec peerPub;
        if (!Handshake::TryParseHello(data, size, peerPub)) return false;

        RF_NETWORK_INFO("Handshake HELLO received from {}:{} (pub=32 bytes)",
            m_endpoint.ipAddress, m_endpoint.port);

        if (!InitializeSession(peerPub)) {
            RF_NETWORK_ERROR("Handshake: InitializeSession failed");
            return true; // consumed (it was a HELLO), even if failed
        }

        // If we haven't sent ours yet, send HELLO back to complete symmetry
        if (!m_handshakeStarted.load(std::memory_order_acquire)) {
            RF_NETWORK_TRACE("Handshake: replying with our HELLO");
            BeginHandshake();
        }
        else {
            RF_NETWORK_TRACE("Handshake: our HELLO already sent");
        }

        RF_NETWORK_INFO("Handshake complete: encryption initialized");
        return true;
    }

    void Connection::FlushPendingSends() {
        // Move pending sends out under lock to avoid recursion holding the lock
        std::deque<PendingSend> local;
        {
            std::lock_guard<std::mutex> lock(m_pendingMtx);
            if (m_pendingSends.empty()) return;
            local.swap(m_pendingSends);
            m_pendingBytes = 0;
        }
        RF_NETWORK_INFO("Flushing {} pre-secure payload(s)", local.size());
        for (auto& ps : local) {
            SendApplicationData(ps.data.data(), static_cast<uint32_t>(ps.data.size()), ps.reliable);
        }
    }

    // ---------------------------------------------------

    void Connection::ProcessIncomingRawPacket(const uint8_t* data, uint32_t size) {
        RF_NETWORK_TRACE("ProcessIncomingRawPacket: size={}", static_cast<size_t>(size));

        // If not initialized yet, check for cleartext handshake
        if (!m_encryptor || !m_encryptor->IsInitialized()) {
            if (MaybeHandleCleartextHandshake(data, size)) {
                return; // handled HELLO
            }
            RF_NETWORK_WARN("Packet received before encryption initialized (non-handshake); dropping");
            return;
        }

        // Secure path: wire = [8-byte nonce BE][ciphertext+tag]
        if (size < 8) {
            RF_NETWORK_WARN("Encrypted frame too small: {} bytes", size);
            return;
        }

        uint64_t nonce_be = 0;
        std::memcpy(&nonce_be, data, sizeof(nonce_be));
        const uint64_t nonce = be64_to_host(nonce_be);

        try {
            std::vector<uint8_t> decrypted_packet;

            if (!m_encryptor->Decrypt({ data + 8, data + size }, decrypted_packet, nonce)) {
                RF_NETWORK_WARN("Decryption failed (auth failure / bad nonce). wire_nonce={}", nonce);
                return;
            }

            // Track last seen rx nonce for diagnostics only
            m_rxNonce.store(nonce, std::memory_order_relaxed);

            HandleDecryptedPacket(decrypted_packet.data(),
                static_cast<uint32_t>(decrypted_packet.size()));
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Exception in ProcessIncomingRawPacket: {}", e.what());
        }
        catch (...) {
            RF_NETWORK_ERROR("Unknown exception in ProcessIncomingRawPacket");
        }
    }

    void Connection::HandleDecryptedPacket(const uint8_t* data, uint32_t size) {
        RF_NETWORK_TRACE("HandleDecryptedPacket: size={}", static_cast<size_t>(size));

        try {
            GeneralPacketHeader generalHeader{};
            ReliabilityPacketHeader reliabilityHeader{};
            const uint8_t* compressed_payload = nullptr;
            uint32_t compressed_payload_size = 0;

            if (!PacketFactory::ParsePacket(data, size, generalHeader, reliabilityHeader,
                compressed_payload, compressed_payload_size)) {
                RF_NETWORK_WARN("Invalid packet format");
                return;
            }

            // Decompress
            std::vector<uint8_t> final_payload =
                m_compressor->Decompress({ compressed_payload, compressed_payload + compressed_payload_size });

            bool processPayload = true;
            if (generalHeader.Type == PacketType::Data_Reliable) {
                if (!UDPReliabilityProtocol::ProcessIncomingHeader(m_reliabilityState, reliabilityHeader)) {
                    RF_NETWORK_TRACE("Duplicate reliable packet ignored");
                    processPayload = false;
                }
            }

            if (processPayload) {
                if (!final_payload.empty()) {
                    if (m_appDataCallback) {
                        m_appDataCallback(final_payload.data(),
                            static_cast<uint32_t>(final_payload.size()));
                    }
                    else {
                        RF_NETWORK_WARN("AppDataCallback not set; dropping {} bytes", final_payload.size());
                    }
                }
                else {
                    RF_NETWORK_TRACE("Final payload empty after decompression; dropping");
                }
            }
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Exception in HandleDecryptedPacket: {}", e.what());
        }
        catch (...) {
            RF_NETWORK_ERROR("Unknown exception in HandleDecryptedPacket");
        }
    }

    void Connection::SendApplicationData(const uint8_t* data, uint32_t size, bool isReliable) {
        RF_NETWORK_TRACE("SendApplicationData: size={} reliable={}", static_cast<size_t>(size), isReliable);

        if (!m_encryptor || !m_encryptor->IsInitialized()) {
            // Queue instead of dropping, and kick handshake.
            BeginHandshake();

            PendingSend ps;
            ps.data.assign(data, data + size);
            ps.reliable = isReliable;

            {
                std::lock_guard<std::mutex> lock(m_pendingMtx);
                m_pendingSends.push_back(std::move(ps));
                m_pendingBytes += size;

                // Backpressure: drop oldest until under limit
                while (m_pendingBytes > kMaxPendingBytes && !m_pendingSends.empty()) {
                    m_pendingBytes -= m_pendingSends.front().data.size();
                    RF_NETWORK_WARN("Pending send queue overflow; dropping oldest payload of {} bytes",
                        m_pendingSends.front().data.size());
                    m_pendingSends.pop_front();
                }
            }

            RF_NETWORK_WARN("Channel not secure yet; queued payload ({} bytes), pending={} bytes",
                size, m_pendingBytes);
            return;
        }

        try {
            // Compress
            std::vector<uint8_t> compressed_payload =
                m_compressor->Compress({ data, data + size });

            // Packetize
            std::vector<uint8_t> packet_bytes;
            if (isReliable) {
                packet_bytes = PacketFactory::CreateReliableDataPacket(
                    m_reliabilityState,
                    compressed_payload.data(),
                    static_cast<uint32_t>(compressed_payload.size()));
            }
            else {
                packet_bytes = PacketFactory::CreateUnreliableDataPacket(
                    compressed_payload.data(),
                    static_cast<uint32_t>(compressed_payload.size()));
            }

            if (!packet_bytes.empty()) {
                SendPacket(packet_bytes);
            }
            else {
                RF_NETWORK_WARN("PacketFactory returned empty packet (reliable={})", isReliable);
            }
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Exception in SendApplicationData: {}", e.what());
        }
        catch (...) {
            RF_NETWORK_ERROR("Unknown exception in SendApplicationData");
        }
    }

    void Connection::SendPacket(const std::vector<uint8_t>& packet_data) {
        RF_NETWORK_TRACE("SendPacket: size={}", packet_data.size());

        try {
            // Prepare per-frame nonce
            const uint64_t tx_nonce = m_txNonce.load(std::memory_order_relaxed);

            // Encrypt (ciphertext does not include nonce)
            std::vector<uint8_t> encrypted_packet =
                m_encryptor->Encrypt(packet_data, tx_nonce);

            if (encrypted_packet.empty()) {
                RF_NETWORK_WARN("Encryption failed: empty packet (nonce={})", tx_nonce);
                return;
            }

            // Build wire: [nonce_be (8)][ciphertext...]
            std::vector<uint8_t> wire;
            wire.resize(8 + encrypted_packet.size());
            uint64_t be = host_to_be64(tx_nonce);
            std::memcpy(wire.data(), &be, sizeof(be));
            std::memcpy(wire.data() + 8, encrypted_packet.data(), encrypted_packet.size());

            // Only bump nonce after successful encrypt + wire build
            m_txNonce.fetch_add(2, std::memory_order_relaxed);

            if (m_sendCallback) {
                m_sendCallback(m_endpoint, wire);
            }
            else {
                RF_NETWORK_WARN("SendCallback not set; dropping {} bytes", wire.size());
            }
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Exception in SendPacket: {}", e.what());
        }
        catch (...) {
            RF_NETWORK_ERROR("Unknown exception in SendPacket");
        }
    }

    void Connection::Update(std::chrono::steady_clock::time_point now) {
        try {
            UDPReliabilityProtocol::ProcessRetransmissions(
                m_reliabilityState, now,
                [this](const std::vector<uint8_t>& retransmit_packet) {
                    // Re-encrypt and send with a NEW nonce each time
                    SendPacket(retransmit_packet);
                }
            );
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Exception in Update: {}", e.what());
        }
        catch (...) {
            RF_NETWORK_ERROR("Unknown exception in Update");
        }
    }

    bool Connection::IsTimedOut(std::chrono::steady_clock::time_point now, std::chrono::seconds timeout) const {
        return UDPReliabilityProtocol::IsConnectionTimedOut(m_reliabilityState, now, timeout);
    }

    bool Connection::IsSecure() const {
        return m_encryptor && m_encryptor->IsInitialized();
    }

    const RiftNet::Networking::NetworkEndpoint& Connection::GetEndpoint() const {
        return m_endpoint;
    }

} // namespace RiftNet::Protocol
