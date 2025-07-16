#pragma once

#include <memory>
#include <vector>
#include <string>
#include "RiftEncrypt.hpp"
#include "riftcompress.hpp"

namespace RiftForged {
    namespace Networking {

        class PacketEncryptorCompressor {
        public:
            PacketEncryptorCompressor(std::unique_ptr<Encryptor> encryptor,
                std::unique_ptr<Compressor> compressor)
                : encryptor_(std::move(encryptor)), compressor_(std::move(compressor)) {
            }

            byte_vec Pack(const byte_vec& payload, const byte_vec& associated_data = {}) {
                auto compressed = compressor_->compress(payload);
                if (compressed.empty()) return {};
                return encryptor_->encrypt(compressed, associated_data);
            }

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
