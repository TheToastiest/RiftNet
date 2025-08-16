#include "pch.h"
#include "Encryptor.hpp"
#include "../../../utilities/logger/Logger.hpp"

#include <cstring>     // std::memcpy
#include <vector>
#include <string>
#include <exception>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#include <intrin.h>
#endif
#if defined(_WIN32)
#include <intrin.h>  // _byteswap_uint64
#else
#include <arpa/inet.h> // htonl
#endif

namespace {

    // Portable host→big-endian 64-bit without relying on htonll from the SDK.
    inline uint64_t host_to_be64(uint64_t x) noexcept {
#if defined(_WIN32)
        // Windows is little-endian on all supported targets.
        return static_cast<uint64_t>(_byteswap_uint64(x));
#else
#if defined(__BYTE_ORDER__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__)
        // Split into 32-bit halves and use htonl on each.
        const uint32_t lo = static_cast<uint32_t>(x & 0xFFFFFFFFull);
        const uint32_t hi = static_cast<uint32_t>(x >> 32);
        return (static_cast<uint64_t>(htonl(lo)) << 32) | htonl(hi);
#else
        // Host is already big-endian.
        return x;
#endif
#endif
    }

} // namespace

namespace RiftNet::Security {

    Encryptor::Encryptor(bool isServerRole)
        : m_isServer(isServerRole) {
        try {
            m_keyExchange = KeyExchangeX25519::generate_keypair();
            RF_NETWORK_DEBUG("Encryptor constructed (role: {}) and keypair generated", m_isServer ? "server" : "client");
        }
        catch (const std::exception& e) {
            RF_NETWORK_CRITICAL("Encryptor::Encryptor keypair generation failed: {}", e.what());
            m_keyExchange = nullptr;
        }
        catch (...) {
            RF_NETWORK_CRITICAL("Encryptor::Encryptor keypair generation failed: unknown exception");
            m_keyExchange = nullptr;
        }
    }

    Encryptor::~Encryptor() = default;

    bool Encryptor::InitializeSession(const byte_vec& remotePublicKey) {
        if (!m_keyExchange) {
            RF_NETWORK_ERROR("Encryptor::InitializeSession: no local keypair; cannot derive session keys");
            m_isInitialized = false;
            return false;
        }

        try {
            std::pair<byte_vec, byte_vec> sessionKeys;
            if (m_isServer) {
                sessionKeys = m_keyExchange->compute_server_session_keys(remotePublicKey);
            }
            else {
                sessionKeys = m_keyExchange->compute_client_session_keys(remotePublicKey);
            }

            // sessionKeys.first = RX key, sessionKeys.second = TX key.
            auto rx_algo = std::make_unique<ChaCha20Poly1305Algorithm>(sessionKeys.first);
            auto tx_algo = std::make_unique<ChaCha20Poly1305Algorithm>(sessionKeys.second);

            m_rxEncryptor = std::make_unique<::Encryptor>(std::move(rx_algo));
            m_txEncryptor = std::make_unique<::Encryptor>(std::move(tx_algo));

            m_isInitialized = (m_rxEncryptor && m_txEncryptor);
            if (!m_isInitialized) {
                RF_NETWORK_ERROR("Encryptor::InitializeSession: failed to construct rx/tx encryptors");
            }
            else {
                RF_NETWORK_INFO("Encryptor session initialized (role: {})", m_isServer ? "server" : "client");
            }
            return m_isInitialized;
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Encryptor::InitializeSession: session key derivation failed: {}", e.what());
            m_isInitialized = false;
            return false;
        }
        catch (...) {
            RF_NETWORK_ERROR("Encryptor::InitializeSession: session key derivation failed: unknown exception");
            m_isInitialized = false;
            return false;
        }
    }

    bool Encryptor::IsInitialized() const {
        return m_isInitialized;
    }

    const byte_vec& Encryptor::GetPublicKey() const {
        static const byte_vec empty_key;
        if (m_keyExchange) {
            return m_keyExchange->get_public_key();
        }
        RF_NETWORK_WARN("Encryptor::GetPublicKey: no local keypair; returning empty key");
        return empty_key;
    }

    std::vector<uint8_t> Encryptor::Encrypt(const std::vector<uint8_t>& plainData, uint64_t nonce) {
        if (!m_isInitialized || !m_txEncryptor) {
            RF_NETWORK_ERROR("Encryptor::Encrypt: not initialized; dropping encrypt request ({} bytes)", plainData.size());
            return {};
        }
        try {
            NonceBuffer expandedNonce = ExpandNonce(nonce);
            byte_vec nonce_vec(expandedNonce.begin(), expandedNonce.end());
            auto out = m_txEncryptor->encrypt_with_nonce(plainData, nonce_vec);
            RF_NETWORK_TRACE("Encrypt ok: in={} bytes, out={} bytes", plainData.size(), out.size());
            return out;
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Encryptor::Encrypt threw: {}", e.what());
            return {};
        }
        catch (...) {
            RF_NETWORK_ERROR("Encryptor::Encrypt threw: unknown exception");
            return {};
        }
    }

    bool Encryptor::Decrypt(const std::vector<uint8_t>& encryptedData,
        std::vector<uint8_t>& outPlainData,
        uint64_t nonce) {
        outPlainData.clear();

        if (!m_isInitialized || !m_rxEncryptor) {
            RF_NETWORK_ERROR("Encryptor::Decrypt: not initialized; dropping decrypt request ({} bytes)", encryptedData.size());
            return false;
        }
        try {
            NonceBuffer expandedNonce = ExpandNonce(nonce);
            byte_vec nonce_vec(expandedNonce.begin(), expandedNonce.end());

            outPlainData = m_rxEncryptor->decrypt_with_nonce(encryptedData, nonce_vec);
            if (outPlainData.empty()) {
                RF_NETWORK_WARN("Encryptor::Decrypt returned empty (possible auth failure / bad nonce)");
                return false;
            }
            RF_NETWORK_TRACE("Decrypt ok: in={} bytes, out={} bytes", encryptedData.size(), outPlainData.size());
            return true;
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Encryptor::Decrypt threw: {}", e.what());
            outPlainData.clear();
            return false;
        }
        catch (...) {
            RF_NETWORK_ERROR("Encryptor::Decrypt threw: unknown exception");
            outPlainData.clear();
            return false;
        }
    }

    NonceBuffer Encryptor::ExpandNonce(uint64_t nonce) const {
        NonceBuffer expanded_nonce{}; // zero-init 12 bytes (IETF ChaCha20-Poly1305)
        const uint64_t nonce_be = host_to_be64(nonce);
        std::memcpy(expanded_nonce.data() + 4, &nonce_be, sizeof(nonce_be));
        RF_NETWORK_TRACE("ExpandNonce: packed 64-bit nonce into 12-byte buffer");
        return expanded_nonce;
    }

} // namespace RiftNet::Security