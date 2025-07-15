// File: Connection.cpp

#include "../../include/core/Connection.hpp"
#include <spdlog/spdlog.h>
#include <cstring>

namespace RiftForged::Networking {

    Connection::Connection(const NetworkEndpoint& remote)
        : endpoint(remote), handshakeComplete(false), nonceRx(1), nonceTx(1),
        localSequence(0), remoteSequence(0), remoteAckBitfield(0), currentNonce(0) {
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

    uint64_t Connection::GenerateUniqueNonce() {
        return encryptNonceBase + (currentNonce++);
    }

    ReliableConnectionState& Connection::GetReliableState() {
        return reliabilityState;
    }

    bool Connection::IsConnected() const {
        return reliabilityState.isConnected;
    }


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

    void Connection::SendPacket(const std::vector<uint8_t>& reliablePayload) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("SendPacket called before handshake complete.");
            return;
        }

        std::vector<uint8_t> encrypted = secureChannel.Encrypt(reliablePayload, nonceTx++);
        if (encrypted.empty()) {
            RF_NETWORK_ERROR("[{}] Encryption failed for SendPacket", endpoint.ToString());
            return;
        }

        if (sendCallback) {
            sendCallback(endpoint, encrypted);
        }
    }

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

    void Connection::SendReliable(const std::vector<uint8_t>& plainData, uint8_t packetType) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Tried to send reliable packet before handshake with {}", endpoint.ToString());
            return;
        }

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

        ReliablePacketHeader header{};
        header.sequenceNumber = localSequence++;
        header.ackNumber = remoteSequence;
        header.ackBitfield = remoteAckBitfield;
        header.packetType = packetType;
        header.nonce = GenerateUniqueNonce();

        std::vector<uint8_t> headerBytes(reinterpret_cast<uint8_t*>(&header),
            reinterpret_cast<uint8_t*>(&header) + sizeof(ReliablePacketHeader));

        std::vector<uint8_t> fullPayload;
        fullPayload.reserve(headerBytes.size() + compressed.size());
        fullPayload.insert(fullPayload.end(), headerBytes.begin(), headerBytes.end());
        fullPayload.insert(fullPayload.end(), compressed.begin(), compressed.end());

        std::vector<uint8_t> encrypted = secureChannel.Encrypt(fullPayload, nonceTx++);
        if (encrypted.empty()) {
            RF_NETWORK_ERROR("Encryption failed for reliable packet to {}", endpoint.ToString());
            return;
        }

        SendRawPacket(encrypted);
    }

    void Connection::HandleRawPacket(const std::vector<uint8_t>& raw) {
        try {
            if (!handshakeComplete) {
                if (raw.size() == 32) {
                    KeyExchange::KeyBuffer remotePub;
                    std::memcpy(remotePub.data(), raw.data(), 32);
                    keyExchange.SetRemotePublicKey(remotePub);

                    KeyExchange::KeyBuffer rxKey, txKey;
                    if (!keyExchange.DeriveSharedKey(true, rxKey, txKey)) {
                        RF_NETWORK_ERROR("Key derivation failed for {}", endpoint.ToString());
                        return;
                    }

                    secureChannel.Initialize(rxKey, txKey);
                    handshakeComplete = true;
                    RF_NETWORK_INFO("Handshake complete with {}", endpoint.ToString());

                    if (sendCallback) {
                        sendCallback(endpoint,
                            std::vector<uint8_t>(GetLocalPublicKey().begin(), GetLocalPublicKey().end()));
                    }
                    return;
                }
                else {
                    RF_NETWORK_WARN("Unexpected packet size before handshake: {}", raw.size());
                    return;
                }
            }

            std::vector<uint8_t> decrypted;
            if (!secureChannel.Decrypt(raw, decrypted, nonceRx++)) {
                RF_NETWORK_WARN("Decryption failed from {}", endpoint.ToString());
                return;
            }

            if (decrypted.size() < sizeof(ReliablePacketHeader)) {
                RF_NETWORK_WARN("[{}] Decrypted too small", endpoint.ToString());
                return;
            }

            ReliablePacketHeader header;
            std::memcpy(&header, decrypted.data(), sizeof(ReliablePacketHeader));

            std::vector<uint8_t> compressedPayload(decrypted.begin() + sizeof(ReliablePacketHeader), decrypted.end());

            Compressor decompressor(std::make_unique<LZ4Algorithm>());
            std::vector<uint8_t> decompressed;
            try {
                decompressed = decompressor.decompress(compressedPayload);
            }
            catch (const std::exception& ex) {
                RF_NETWORK_ERROR("[{}] Decompression failed: {}", endpoint.ToString(), ex.what());
                return;
            }

            if (decompressed.empty()) {
                RF_NETWORK_WARN("[{}] Empty decompressed payload", endpoint.ToString());
                return;
            }

            RF_NETWORK_DEBUG("[{}] Decompressed size: {}", endpoint.ToString(), decompressed.size());

            std::string msg(decompressed.begin(), decompressed.end());
            RF_NETWORK_INFO("[{}] > {}", endpoint.ToString(), msg);

            std::string echoStr = "[ECHO] " + msg;
            std::vector<uint8_t> echoVec(echoStr.begin(), echoStr.end());
            SendReliable(echoVec, static_cast<uint8_t>(PacketType::EchoTest));

        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Exception in HandleRawPacket: {}", ex.what());
        }
    }

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
