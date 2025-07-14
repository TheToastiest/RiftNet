// File: Connection.cpp

#include "../../include/core/Connection.hpp"
#include <spdlog/spdlog.h>
#include <cstring>
#include <chrono>

using namespace std::chrono;

namespace RiftForged::Networking {

    using Clock = steady_clock;
    using TimePoint = Clock::time_point;

    namespace {
        constexpr double ALPHA = 0.125;
        constexpr double BETA = 0.25;
        constexpr uint64_t INITIAL_RTO_MS = 200;
    }

    Connection::Connection(const NetworkEndpoint& remote)
        : endpoint(remote), handshakeComplete(false), nonceRx(1), nonceTx(1),
        localSequence(0), remoteSequence(0), remoteAckBitfield(0), currentNonce(0),
        srttMs(INITIAL_RTO_MS), rttVarMs(INITIAL_RTO_MS / 2), rtoMs(INITIAL_RTO_MS) {
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

    void Connection::SendUnencrypted(const std::vector<uint8_t>& data) {
        if (sendCallback) sendCallback(endpoint, data);
    }

    void Connection::SendRawPacket(const std::vector<uint8_t>& packet) {
        if (sendCallback) sendCallback(endpoint, packet);
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

        SendRawPacket(encrypted);
    }

    void Connection::SendSecure(const std::vector<uint8_t>& data) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Secure send before handshake with {}", endpoint.ToString());
            return;
        }

        Compressor compressor(std::make_unique<LZ4Algorithm>());
        std::vector<uint8_t> compressed;
        try {
            compressed = compressor.compress(data);
        }
        catch (const std::exception& ex) {
            RF_NETWORK_ERROR("Compression failed for {}: {}", endpoint.ToString(), ex.what());
            return;
        }

        std::vector<uint8_t> encrypted = secureChannel.Encrypt(compressed, nonceTx++);
        if (encrypted.empty()) {
            RF_NETWORK_WARN("[{}] Encryption failed. Nothing sent.", endpoint.ToString());
            return;
        }

        SendRawPacket(encrypted);
    }

    void Connection::SendReliable(const std::vector<uint8_t>& plainData, uint8_t packetType) {
        if (!handshakeComplete) {
            RF_NETWORK_WARN("Reliable send before handshake with {}", endpoint.ToString());
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

        uint64_t nonce = GenerateUniqueNonce();
        std::vector<uint8_t> encrypted = secureChannel.Encrypt(compressed, nonce);
        if (encrypted.empty()) {
            RF_NETWORK_ERROR("Encryption failed for reliable packet to {}", endpoint.ToString());
            return;
        }

        PlainPacketHeader header{};
        header.packetType = packetType;
        header.nonce = nonce;
        header.payloadSize = static_cast<uint32_t>(encrypted.size());

        std::vector<uint8_t> fullPacket;
        fullPacket.reserve(sizeof(header) + encrypted.size());
        fullPacket.insert(fullPacket.end(), reinterpret_cast<uint8_t*>(&header),
            reinterpret_cast<uint8_t*>(&header) + sizeof(header));
        fullPacket.insert(fullPacket.end(), encrypted.begin(), encrypted.end());

        // Record send time for RTT tracking
        auto sendTime = Clock::now();
        rttTimestamps[localSequence] = sendTime;

        SendRawPacket(fullPacket);
        localSequence++;
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
                    RF_NETWORK_WARN("Unexpected pre-handshake packet size: {}", raw.size());
                    return;
                }
            }

            if (raw.size() < sizeof(PlainPacketHeader)) {
                RF_NETWORK_WARN("Invalid packet size from {}", endpoint.ToString());
                return;
            }

            PlainPacketHeader header{};
            std::memcpy(&header, raw.data(), sizeof(header));

            if (raw.size() < sizeof(header) + header.payloadSize) {
                RF_NETWORK_WARN("Payload truncated from {}", endpoint.ToString());
                return;
            }

            std::vector<uint8_t> encryptedPayload(raw.begin() + sizeof(header),
                raw.begin() + sizeof(header) + header.payloadSize);
            std::vector<uint8_t> decrypted;
            if (!secureChannel.Decrypt(encryptedPayload, decrypted, header.nonce)) {
                RF_NETWORK_WARN("Decryption failed from {}", endpoint.ToString());
                return;
            }

            if (decrypted.size() < sizeof(ReliablePacketHeader)) {
                RF_NETWORK_WARN("[{}] Decrypted payload too small", endpoint.ToString());
                return;
            }

            ReliablePacketHeader reliableHeader;
            std::memcpy(&reliableHeader, decrypted.data(), sizeof(reliableHeader));

            // RTT Estimation if ACK matches our previously sent packet
            auto it = rttTimestamps.find(reliableHeader.ackNumber);
            if (it != rttTimestamps.end()) {
                auto now = Clock::now();
                auto rttSampleMs = duration_cast<milliseconds>(now - it->second).count();
                rttTimestamps.erase(it);

                // RFC 6298 RTT estimation
                if (srttMs == INITIAL_RTO_MS) {
                    srttMs = rttSampleMs;
                    rttVarMs = rttSampleMs / 2;
                }
                else {
                    rttVarMs = (1 - BETA) * rttVarMs + BETA * std::abs(srttMs - rttSampleMs);
                    srttMs = (1 - ALPHA) * srttMs + ALPHA * rttSampleMs;
                }
                rtoMs = srttMs + std::max<int64_t>(1, 4 * rttVarMs);
                RF_NETWORK_DEBUG("Updated RTT: {} ms | RTO: {} ms", srttMs, rtoMs);
            }

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
