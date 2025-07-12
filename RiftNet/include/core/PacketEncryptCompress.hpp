#pragma once

#include <memory>
#include <vector>
#include <string>
#include "RiftEncrypt.hpp"
#include "riftforged_compress.hpp"

namespace RiftForged {
    namespace Networking {

        /**
         * @class PacketEncryptorCompressor
         * @brief High-level pipeline wrapper for Compress → Encrypt and Decrypt → Decompress.
         */
        class PacketEncryptorCompressor {
        public:
            PacketEncryptorCompressor(std::unique_ptr<Encryptor> encryptor,
                std::unique_ptr<Compressor> compressor)
                : encryptor_(std::move(encryptor)), compressor_(std::move(compressor)) {
            }

            /**
             * @brief Packs a raw payload through compression and encryption.
             * @param payload Raw byte vector (uncompressed, unencrypted).
             * @param associated_data Optional AAD for AEAD encryption.
             * @return Encrypted + compressed byte stream.
             */
            byte_vec Pack(const byte_vec& payload, const byte_vec& associated_data = {}) {
                auto compressed = compressor_->compress(payload);
                if (compressed.empty()) return {};
                return encryptor_->encrypt(compressed, associated_data);
            }

            /**
             * @brief Unpacks a received byte stream through decryption and decompression.
             * @param encrypted_payload Encrypted + compressed byte vector.
             * @param associated_data Optional AAD used during encryption.
             * @return Original uncompressed payload.
             */
            byte_vec Unpack(const byte_vec& encrypted_payload, const byte_vec& associated_data = {}) {
                auto decrypted = encryptor_->decrypt(encrypted_payload, associated_data);
                if (decrypted.empty()) return {};
                return compressor_->decompress(decrypted);
            }

        private:
            std::unique_ptr<Encryptor> encryptor_;
            std::unique_ptr<Compressor> compressor_;
        };

    } // namespace Networking
} // namespace RiftForged
