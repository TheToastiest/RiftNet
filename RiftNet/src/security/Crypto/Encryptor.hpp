#pragma once

#include "riftencrypt.hpp"
#include <array>
#include <cstdint>
#include <memory>
#include <vector>

// Forward declare classes from the global namespace
class Encryptor;
class KeyExchangeX25519;

namespace RiftNet::Security {

    // 32-byte symmetric key (AES-256, ChaCha20)
    using KeyBuffer = std::array<uint8_t, 32>;
    // 12-byte nonce (ChaCha20-Poly1305 IETF standard, AES-GCM standard)
    using NonceBuffer = std::array<uint8_t, 12>;

    /**
     * @class Encryptor
     * @brief Manages a secure, encrypted communication channel between two peers.
     * This class handles the X25519 key exchange and the subsequent symmetric
     * encryption/decryption of data.
     */
    class Encryptor {
    public:
        /**
         * @brief Constructs the Encryptor.
         * @param isServerRole True if this instance is on the server, false for a client.
         * This determines which key derivation function to use.
         */
        explicit Encryptor(bool isServerRole);
        ~Encryptor(); // Required for unique_ptr to incomplete type

        /**
         * @brief Computes the shared session keys and initializes the symmetric ciphers.
         * @param remotePublicKey The public key received from the remote peer.
         * @return True on success, false on failure.
         */
        bool InitializeSession(const byte_vec& remotePublicKey);

        /**
         * @brief Checks if the session has been successfully initialized.
         */
        bool IsInitialized() const;

        /**
         * @brief Gets the local public key for this instance.
         * This key should be sent to the remote peer during the handshake.
         * @return A const reference to the public key.
         */
        const byte_vec& GetPublicKey() const;

        /**
         * @brief Encrypts a block of data using the derived session key.
         * @param plainData The data to encrypt.
         * @param nonce A unique, 64-bit nonce for this specific message.
         * @return A vector containing the ciphertext. Returns an empty vector on failure.
         */
        std::vector<uint8_t> Encrypt(const std::vector<uint8_t>& plainData, uint64_t nonce);

        /**
         * @brief Decrypts a block of data using the derived session key.
         * @param encryptedData The ciphertext to decrypt.
         * @param outPlainData A vector that will be filled with the decrypted plaintext.
         * @param nonce The same 64-bit nonce that was used to encrypt the message.
         * @return True on successful decryption and authentication, false otherwise.
         */
        bool Decrypt(const std::vector<uint8_t>& encryptedData, std::vector<uint8_t>& outPlainData, uint64_t nonce);

    private:
        /**
         * @brief Expands a 64-bit nonce into a 12-byte nonce suitable for the cipher.
         */
        NonceBuffer ExpandNonce(uint64_t nonce) const;

        // Asymmetric key exchange object
        std::unique_ptr<KeyExchangeX25519> m_keyExchange;

        // Symmetric ciphers for data transfer, initialized after key exchange
        std::unique_ptr<::Encryptor> m_rxEncryptor;
        std::unique_ptr<::Encryptor> m_txEncryptor;

        bool m_isServer;
        bool m_isInitialized = false;
    };

} // namespace RiftNet::Security
