// File: KeyExchange.cpp

#include "../../include/core/KeyExchange.hpp"
#include <sodium.h>
#include <stdexcept>

namespace RiftNet::Networking {

    KeyExchange::KeyExchange() {
        // Generate ephemeral X25519 keypair
        crypto_kx_keypair(localPublicKey.data(), localPrivateKey.data());
    }

    const KeyExchange::KeyBuffer& KeyExchange::GetLocalPublicKey() const {
        return localPublicKey;
    }

    void KeyExchange::SetRemotePublicKey(const KeyBuffer& remotePubKey) {
        this->remotePublicKey = remotePubKey;
    }

    bool KeyExchange::DeriveSharedKey(bool isServer, KeyBuffer& outRxKey, KeyBuffer& outTxKey) {
        if (isServer) {
            if (crypto_kx_server_session_keys(
                outRxKey.data(),  // Receive key (client sends to server)
                outTxKey.data(),  // Transmit key (server sends to client)
                localPublicKey.data(),
                localPrivateKey.data(),
                remotePublicKey.data()) != 0) {
                return false; // Derivation failed
            }
        }
        else {
            if (crypto_kx_client_session_keys(
                outRxKey.data(),  // Receive key (server sends to client)
                outTxKey.data(),  // Transmit key (client sends to server)
                localPublicKey.data(),
                localPrivateKey.data(),
                remotePublicKey.data()) != 0) {
                return false; // Derivation failed
            }
        }

        return true;
    }

}
