#include "../../include/core/SecureChannel.hpp"
#include "riftencrypt.hpp"

namespace RiftForged::Networking {

    SecureChannel::SecureChannel() = default;

    void SecureChannel::Initialize(const KeyBuffer& rx, const KeyBuffer& tx) {
        rxKey = rx;
        txKey = tx;
        initialized = true;

        byte_vec rxVec(rx.begin(), rx.end());
        byte_vec txVec(tx.begin(), tx.end());

        rxCipher = std::make_unique<ChaCha20Poly1305Algorithm>(rxVec);
        txCipher = std::make_unique<ChaCha20Poly1305Algorithm>(txVec);
    }

    bool SecureChannel::IsInitialized() const {
        return initialized;
    }

    NonceBuffer SecureChannel::ExpandNonce(uint64_t nonce) const {
        NonceBuffer nonceBuf{};
        // Write the 64-bit nonce into the last 8 bytes (big-endian preferred for cross-platform)
        for (int i = 0; i < 8; ++i) {
            nonceBuf[11 - i] = static_cast<uint8_t>((nonce >> (i * 8)) & 0xFF);
        }
        return nonceBuf;
    }

    std::vector<uint8_t> SecureChannel::Encrypt(const std::vector<uint8_t>& plainData, uint64_t nonce) {
        if (!initialized || !txCipher) return {};
        auto expandedNonce = ExpandNonce(nonce);
        byte_vec nonceVec(expandedNonce.begin(), expandedNonce.end());
        return txCipher->encrypt_with_nonce(plainData, nonceVec, {});  // optional AAD is empty
    }

    bool SecureChannel::Decrypt(const std::vector<uint8_t>& encryptedData, std::vector<uint8_t>& outPlainData, uint64_t nonce) {
        if (!initialized || !rxCipher) return false;
        auto expandedNonce = ExpandNonce(nonce);
        byte_vec nonceVec(expandedNonce.begin(), expandedNonce.end());
        auto result = rxCipher->decrypt_with_nonce(encryptedData, nonceVec, {});
        if (result.empty()) return false;
        outPlainData = std::move(result);
        return true;
    }

} // namespace RiftForged::Networking
