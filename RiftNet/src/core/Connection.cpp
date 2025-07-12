// File: Connection.cpp

#include "../../include/core/Connection.hpp"
#include <spdlog/spdlog.h>
#include <cstring> // for memcpy

namespace RiftForged::Networking {

    Connection::Connection(const NetworkEndpoint& remote)
        : endpoint(remote), handshakeComplete(false), nonceRx(1), nonceTx(1) {
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

    void Connection::HandleRawPacket(const std::vector<uint8_t>& raw) {
        try {
            RF_NETWORK_DEBUG("[{}] HandleRawPacket received {} bytes", endpoint.ToString(), raw.size());

            // ---- Step 1: Handshake check ----
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
                    RF_NETWORK_WARN("Unexpected packet size before handshake: {} bytes from {}",
                        raw.size(), endpoint.ToString());
                    return;
                }
            }

            // ---- Step 2: Decrypt ----
            std::vector<uint8_t> decrypted;
            if (!secureChannel.Decrypt(raw, decrypted, nonceRx++)) {
                RF_NETWORK_WARN("Decryption failed from {}", endpoint.ToString());
                return;
            }

            if (decrypted.empty()) {
                RF_NETWORK_WARN("[{}] Decrypted payload was empty.", endpoint.ToString());
                return;
            }

            RF_NETWORK_DEBUG("[{}] Decrypted size: {}", endpoint.ToString(), decrypted.size());

            // ---- Step 3: Decompress ----
            std::vector<uint8_t> decompressed;
            Compressor decompressor(std::make_unique<LZ4Algorithm>());
            try {
                decompressed = decompressor.decompress(decrypted);
            }
            catch (const std::exception& ex) {
                RF_NETWORK_ERROR("[{}] LZ4 decompression exception: {}", endpoint.ToString(), ex.what());
                return;
            }

            if (decompressed.empty()) {
                RF_NETWORK_WARN("[{}] Decompression yielded empty result.", endpoint.ToString());
                return;
            }

            RF_NETWORK_DEBUG("[{}] Decompressed size: {}", endpoint.ToString(), decompressed.size());

            // ---- Step 4: Interpret ----
            std::string msg(decompressed.begin(), decompressed.end());
            RF_NETWORK_INFO("[{}] > {}", endpoint.ToString(), msg);

            // === Step 5: ECHO ROUNDTRIP TEST ===
            // Modify or simply echo the same string back
            std::string echoStr = "[ECHO] " + msg;
            std::vector<uint8_t> echoVec(echoStr.begin(), echoStr.end());

            SendSecure(echoVec); // will compress + encrypt + send

        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Exception in HandleRawPacket from {}: {}", endpoint.ToString(), ex.what());
        }
    }


    void Connection::SendUnencrypted(const std::vector<uint8_t>& data) {
        if (sendCallback) {
            sendCallback(endpoint, data);
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

    void Connection::PerformKeyExchange(const KeyExchange::KeyBuffer& clientPubKey, bool isServer) {
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
