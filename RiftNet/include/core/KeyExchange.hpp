// File: KeyExchange.hpp

#pragma once

#include <array>
#include <sodium.h>

namespace RiftNet::Networking {

    class KeyExchange {
    public:
        static constexpr size_t KEY_SIZE = crypto_kx_PUBLICKEYBYTES;

        using KeyBuffer = std::array<unsigned char, KEY_SIZE>;

        KeyExchange();

        const KeyBuffer& GetLocalPublicKey() const;
        void SetRemotePublicKey(const KeyBuffer& remotePubKey);
        bool DeriveSharedKey(bool isServer, KeyBuffer& outRxKey, KeyBuffer& outTxKey);

    private:
        KeyBuffer localPublicKey{};
        KeyBuffer localPrivateKey{};
        KeyBuffer remotePublicKey{};
    };

} // namespace RiftForged::Networking
