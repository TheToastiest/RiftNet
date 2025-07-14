// File: KeyExchange.hpp

#pragma once

#include <array>
#include <sodium.h>

namespace RiftForged::Networking {

    class KeyExchange {
    public:
        static constexpr size_t KEY_SIZE = crypto_kx_PUBLICKEYBYTES;

        using KeyBuffer = std::array<unsigned char, KEY_SIZE>;

        KeyExchange();

        const KeyBuffer& GetLocalPublicKey() const;
        void SetRemotePublicKey(const KeyBuffer& remotePubKey);

        /**
         * @brief Derives symmetric keys for encryption and decryption.
         * @param isServer True if this endpoint is the server.
         * @param outRxKey Receive key (for decryption).
         * @param outTxKey Transmit key (for encryption).
         * @return True on success.
         */
        bool DeriveSharedKey(bool isServer, KeyBuffer& outRxKey, KeyBuffer& outTxKey);

    private:
        KeyBuffer localPublicKey{};
        KeyBuffer localPrivateKey{};
        KeyBuffer remotePublicKey{};
    };

} // namespace RiftForged::Networking
