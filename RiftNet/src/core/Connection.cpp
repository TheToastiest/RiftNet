// File: Connection.cpp

#include "../../include/core/Connection.hpp"
#include "../../include/core/protocols.hpp"            // <- replaces NetworkTypes.hpp
#include "../../include/core/UDPReliabilityProtocol.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <mutex>
#include <algorithm>

using namespace RiftNet::Protocol;

namespace RiftForged::Networking {

    Connection::Connection(const NetworkEndpoint& remote, bool isServerRole)
        : endpoint(remote)
        , handshakeComplete(false)
        , nonceRx(1)
        , nonceTx(1)
        , localSequence(0)
        , remoteSequence(0)
        , remoteAckBitfield(0)
        , currentNonce(0)
        , isServerRole_(isServerRole) {
    }

    const NetworkEndpoint& Connection::GetRemoteAddress() const {
        return endpoint;
    }

    const KeyExchange::KeyBuffer& Connection::GetLocalPublicKey() const {
        return keyExchange.GetLocalPublicKey();
    }

    void Connection::SetSendCallback(SendCallback cb) {
        sendCallback = std::move(cb);
    }

    void Connection::SetAppPacketCallback(AppPacketCallback cb) {
        appCallback = std::move(cb);
    }

    uint64_t Connection::GenerateUniqueNonce() {
        return encryptNonceBase + (currentNonce++);
    }

    ReliableConnectionState& Connection::GetReliableState() {
        return reliabilityState;
    }

    bool Connection::IsConnected() const {
        return reliabilityState.isConnected;
    }

    // ----------------- Low-level send helpers -----------------

    void Connection::SendUnencrypted(const std::vector<uint8_t>& data) {
        if (sendCallback) {
            sendCallback(endpoint, data);
        }
    }

    void Connection::SendRawPacket(const std::vector<uint8_t>& packet) {
        if (sendCallback) {
            sendCallback(endpoint, packet);
        }
    }

    // New canonical framed+encrypted path
    void Connection::SendFramed(const std::vector<uint8_t>& framedWire) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("SendFramed called before handshake complete.");
            return;
        }
        if (framedWire.empty()) return;

        // Encrypt whole framed wire with channel TX nonce
        std::vector<uint8_t> encrypted = secureChannel.Encrypt(framedWire, nonceTx++);
        if (encrypted.empty()) {
            RF_NETWORK_ERROR("[{}] Encryption failed for SendFramed", endpoint.ToString());
            return;
        }
        if (sendCallback) sendCallback(endpoint, encrypted);

        reliabilityState.lastPacketSentTime = std::chrono::steady_clock::now();
    }

    // Legacy API kept: uncompressed reliable send
    void Connection::SendPacket(const std::vector<uint8_t>& payload, uint8_t packetTypeU8) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("SendPacket called before handshake complete.");
            return;
        }

        const PacketType pktType = static_cast<PacketType>(packetTypeU8);
        auto& state = reliabilityState;

        // Prepare framed packet (outer 11B + reliable 17B + body)
        const uint64_t headerNonce = GenerateUniqueNonce();
        auto frames = UDPReliabilityProtocol::PrepareOutgoingPacketsFramed(
            state, pktType, payload.data(),
            static_cast<uint32_t>(payload.size()),
            headerNonce
        );
        if (frames.empty()) return;

        // Encrypt & send each frame (single frame in current path)
        for (auto& f : frames) SendFramed(f);
    }

    // Preferred API: compressed reliable send
    void Connection::SendReliable(const std::vector<uint8_t>& plainData, uint8_t packetTypeU8) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Tried to send reliable packet before handshake with {}", endpoint.ToString());
            return;
        }

        // Compress payload
        Compressor compressor(std::make_unique<LZ4Algorithm>());
        std::vector<uint8_t> compressed;
        try {
            compressed = compressor.compress(plainData);
        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Compression failed for {}: {}", endpoint.ToString(), ex.what());
            return;
        }
        if (compressed.empty()) {
            RF_NETWORK_WARN("Compression yielded no data for {}", endpoint.ToString());
            return;
        }

        const PacketType pktType = static_cast<PacketType>(packetTypeU8);
        auto& state = reliabilityState;

        // Frame with reliability headers
        const uint64_t headerNonce = GenerateUniqueNonce();
        auto frames = UDPReliabilityProtocol::PrepareOutgoingPacketsFramed(
            state, pktType, compressed.data(),
            static_cast<uint32_t>(compressed.size()),
            headerNonce
        );
        if (frames.empty()) return;

        for (auto& f : frames) SendFramed(f);
    }

    // ----------------- Secure raw send (non-reliable) -----------------

    void Connection::SendSecure(const std::vector<uint8_t>& data) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Attempted to send secure message before handshake with {}", endpoint.ToString());
            return;
        }

        std::vector<uint8_t> compressed;
        Compressor compressor(std::make_unique<LZ4Algorithm>());
        try {
            compressed = compressor.compress(data);
        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Compression failed for {}: {}", endpoint.ToString(), ex.what());
            return;
        }
        if (compressed.empty()) {
            RF_NETWORK_WARN("Compression failed for outgoing message to {}", endpoint.ToString());
            return;
        }

        std::vector<uint8_t> encrypted = secureChannel.Encrypt(compressed, nonceTx++);
        if (encrypted.empty()) {
            RF_NETWORK_WARN("[{}] Encryption failed. Nothing sent.", endpoint.ToString());
            return;
        }

        if (sendCallback) {
            sendCallback(endpoint, encrypted);
        }
    }

    // ----------------- Receive path -----------------

    void Connection::HandleRawPacket(const std::vector<uint8_t>& raw) {
        try {
            // ---- Handshake (X25519 pubkey exchange) ----
            if (!handshakeComplete) {
                if (raw.size() == 32) {
                    KeyExchange::KeyBuffer remotePub;
                    std::memcpy(remotePub.data(), raw.data(), 32);
                    keyExchange.SetRemotePublicKey(remotePub);

                    KeyExchange::KeyBuffer rxKey, txKey;
                    if (!keyExchange.DeriveSharedKey(isServerRole_, rxKey, txKey)) {
                        RF_NETWORK_ERROR("Key derivation failed for {}", endpoint.ToString());
                        return;
                    }
                    secureChannel.Initialize(rxKey, txKey);
                    handshakeComplete = true;
                    RF_NETWORK_INFO("Handshake complete (role: {}) with {}",
                        isServerRole_ ? "server" : "client", endpoint.ToString());
                    return;
                }
                else {
                    RF_NETWORK_WARN("Unexpected packet size before handshake: {}", raw.size());
                    return;
                }
            }

            // ---- Secure path: decrypt whole frame ----
            std::vector<uint8_t> decrypted;
            if (!secureChannel.Decrypt(raw, decrypted, nonceRx++)) {
                RF_NETWORK_WARN("Decryption failed from {}", endpoint.ToString());
                return;
            }

            // ---- Parse outer+reliable headers, update reliability, extract body ----
            PacketType pktId{};

            std::vector<uint8_t> bodyCompressed;

            const bool ok = UDPReliabilityProtocol::ProcessIncomingWire(
                reliabilityState,
                decrypted.data(),
                static_cast<uint32_t>(decrypted.size()),
                pktId,
                bodyCompressed);

            // dup/out-of-window, or otherwise consumed by reliability → nothing to do
            if (!ok) {
                return;
            }

            // control frames expected to be empty
            if (pktId == PacketType::ReliableAck ||
                (pktId == PacketType::Heartbeat && bodyCompressed.empty()))
            {
                return;
            }

            // unexpected empties (real issue worth logging)
            if (bodyCompressed.empty()) {
                RF_NETWORK_DEBUG("[{}] Empty payload (unexpected type={})",
                    endpoint.ToString(), static_cast<int>(pktId));
                return;
            }

            // ---- Decompress app payload ----
            std::vector<uint8_t> decompressed;
            {
                Compressor decompressor(std::make_unique<LZ4Algorithm>());
                try {
                    decompressed = decompressor.decompress(bodyCompressed);
                }
                catch (const std::exception& ex) {
                    RF_NETWORK_ERROR("[{}] Decompression failed: {}", endpoint.ToString(), ex.what());
                    return;
                }
            }
            if (decompressed.empty()) {
                RF_NETWORK_WARN("[{}] Empty decompressed payload", endpoint.ToString());
                return;
            }

            // ---- App dispatch ----
            if (appCallback) {
                appCallback(endpoint.ToString(),
                    static_cast<uint8_t>(pktId),
                    decompressed.data(),
                    decompressed.size());
                return;
            }

            // ---- Fallback echo (dev path) ----
            {
                std::string msg(reinterpret_cast<const char*>(decompressed.data()), decompressed.size());
                RF_NETWORK_INFO("[{}] > {}", endpoint.ToString(), msg);

                std::string echoStr = "[ECHO] " + msg;
                std::vector<uint8_t> echoVec(echoStr.begin(), echoStr.end());
                SendReliable(echoVec, static_cast<uint8_t>(PacketType::EchoTest));
            }

        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Exception in HandleRawPacket: {}", ex.what());
        }
    }

    // ----------------- Key exchange (manual path) -----------------

    uint64_t GenerateSecureRandomNonce64() {
        uint64_t result;
        randombytes_buf(&result, sizeof(result));
        return result;
    }

    void Connection::PerformKeyExchange(const KeyExchange::KeyBuffer& clientPubKey, bool isServer) {
        encryptNonceBase = GenerateSecureRandomNonce64();
        keyExchange.SetRemotePublicKey(clientPubKey);

        KeyExchange::KeyBuffer rxKey, txKey;
        if (!keyExchange.DeriveSharedKey(isServer, rxKey, txKey)) {
            RF_NETWORK_ERROR("Manual key exchange failed for {}", endpoint.ToString());
            return;
        }
        secureChannel.Initialize(rxKey, txKey);
        handshakeComplete = true;
        RF_NETWORK_INFO("Manual handshake complete with {}", endpoint.ToString());
    }

} // namespace RiftForged::Networking
