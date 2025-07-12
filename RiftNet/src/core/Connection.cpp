// File: Connection.cpp

#include "../../include/core/Connection.hpp"
#include <spdlog/spdlog.h>
#include <cstring> // for memcpy

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


    void Connection::HandleRawPacket(const std::vector<uint8_t>& raw) {
        try {
            RF_NETWORK_DEBUG("[{}] HandleRawPacket received {} bytes", endpoint.ToString(), raw.size());

            // === HANDSHAKE PHASE ===
            if (!handshakeComplete) {
                if (raw.size() == 32) {
                    KeyExchange::KeyBuffer remotePub;
                    std::memcpy(remotePub.data(), raw.data(), 32);
                    keyExchange.SetRemotePublicKey(remotePub);

                    KeyExchange::KeyBuffer rxKey, txKey;
                    if (!keyExchange.DeriveSharedKey(true /* isServer */, rxKey, txKey)) {
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

            // === DECRYPT ===
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

            // === DECOMPRESS ===
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

            // === RELIABILITY TRACKING ===
            ReliableConnectionState& state = reliabilityState;
            uint16_t seq = header.sequenceNumber;

            if (seq <= state.highestReceivedSequenceNumber &&
                ((state.receivedSequenceBitfield >> (state.highestReceivedSequenceNumber - seq)) & 1)) {
                RF_NETWORK_DEBUG("[{}] Dropped duplicate seq {}", endpoint.ToString(), seq);
                return;
            }

            if (seq > state.highestReceivedSequenceNumber) {
                uint16_t shift = seq - state.highestReceivedSequenceNumber;
                state.receivedSequenceBitfield = (state.receivedSequenceBitfield << shift) | 1;
                state.highestReceivedSequenceNumber = seq;
            }
            else {
                state.receivedSequenceBitfield |= (1 << (state.highestReceivedSequenceNumber - seq));
            }

            // === Display or Process ===
            std::string msg(decompressed.begin(), decompressed.end());
            RF_NETWORK_INFO("[{}] > {}", endpoint.ToString(), msg);

            // === Echo back ===
            std::string echoStr = "[ECHO] " + msg;
            std::vector<uint8_t> echoVec(echoStr.begin(), echoStr.end());
            SendReliable(echoVec, static_cast<uint8_t>(PacketType::EchoTest));

        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Exception in HandleRawPacket: {}", ex.what());
        }
    }

    void Connection::SendReliable(const std::vector<uint8_t>& plainData, uint8_t packetType) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Tried to send reliable packet before handshake with {}", endpoint.ToString());
            return;
        }

        // ---- Step 1: Compress Payload ----
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

        // ---- Step 2: Build Reliable Header ----
        ReliablePacketHeader header{};
        header.sequenceNumber = localSequence++;
        header.ackNumber = remoteSequence;
        header.ackBitfield = remoteAckBitfield;
        header.packetType = packetType;
        header.nonce = GenerateUniqueNonce();

        // ---- Step 3: Pack Header + Compressed Payload ----
        const uint8_t* headerPtr = reinterpret_cast<const uint8_t*>(&header);
        std::vector<uint8_t> headerBytes(headerPtr, headerPtr + sizeof(ReliablePacketHeader));

        std::vector<uint8_t> fullPayload;
        fullPayload.reserve(headerBytes.size() + compressed.size());
        fullPayload.insert(fullPayload.end(), headerBytes.begin(), headerBytes.end());
        fullPayload.insert(fullPayload.end(), compressed.begin(), compressed.end());

        // ---- Step 4: Encrypt ----
        std::vector<uint8_t> encrypted = secureChannel.Encrypt(fullPayload, nonceTx++);
        if (encrypted.empty()) {
            RF_NETWORK_ERROR("Encryption failed for reliable packet to {}", endpoint.ToString());
            return;
        }

        RF_NETWORK_DEBUG("[{}] Sending Reliable packetType {} | Plain: {} | Compressed: {} | Encrypted: {}",
            endpoint.ToString(), packetType, plainData.size(), compressed.size(), encrypted.size());

        // ---- Step 5: Save for Reliability Tracking ----
        uint64_t currentTime = static_cast<uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
        ReliablePacket pkt(header.sequenceNumber, packetType, header.nonce, encrypted, currentTime);
        reliabilityMap[header.sequenceNumber] = std::move(pkt);

        // ---- Step 6: Send ----
        SendRawPacket(encrypted);
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

    void Connection::SendSecure(const std::vector<uint8_t>& data) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Attempted to send secure message before handshake with {}", endpoint.ToString());
            return;
        }

        RF_NETWORK_DEBUG("[{}] Sending raw message of {} bytes", endpoint.ToString(), data.size());

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

        RF_NETWORK_DEBUG("[{}] Compressed size: {}", endpoint.ToString(), compressed.size());

        std::vector<uint8_t> encrypted = secureChannel.Encrypt(compressed, nonceTx++);

        if (encrypted.empty()) {
            RF_NETWORK_WARN("[{}] Encryption failed. Nothing sent.", endpoint.ToString());
            return;
        }

        RF_NETWORK_DEBUG("[{}] Encrypted size: {}", endpoint.ToString(), encrypted.size());

        if (sendCallback) {
            sendCallback(endpoint, encrypted);
        }
    }

    uint64_t GenerateSecureRandomNonce64() {
        uint64_t result;
        randombytes_buf(&result, sizeof(result));
        return result;
    }

    void Connection::PerformKeyExchange(const KeyExchange::KeyBuffer& clientPubKey, bool isServer) {
        encryptNonceBase = GenerateSecureRandomNonce64(); // Your helper should return a uint64_t
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
