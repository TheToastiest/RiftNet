#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <vector>
#include <riftencrypt.hpp>

namespace RiftForged::Networking {

    // 32-byte symmetric key
    using KeyBuffer = std::array<uint8_t, 32>;
    // 12-byte nonce (ChaCha20-Poly1305 IETF standard)
    using NonceBuffer = std::array<uint8_t, 12>;

    class SecureChannel {
    public:
        SecureChannel();

        void Initialize(const KeyBuffer& rxKey, const KeyBuffer& txKey);

        bool IsInitialized() const;

        // Encrypt with a 64-bit nonce (converted to a 12-byte nonce internally)
        std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plainData, uint64_t nonce);

        // Decrypt with the same 64-bit nonce used to encrypt
        bool Decrypt(const std::vector<uint8_t>& encryptedData, std::vector<uint8_t>& outPlainData, uint64_t nonce);

    private:
        KeyBuffer rxKey;
        KeyBuffer txKey;

        std::unique_ptr<CryptoAlgorithm> rxCipher;
        std::unique_ptr<CryptoAlgorithm> txCipher;

        bool initialized = false;

        NonceBuffer ExpandNonce(uint64_t nonce) const;
    };

} // namespace RiftForged::Networking
