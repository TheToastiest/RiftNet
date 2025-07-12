// File: Connection.hpp

#pragma once

#include "Logger.hpp"
#include "NetworkTypes.hpp"
#include "NetworkEndpoint.hpp"
#include "HandshakePacket.hpp"
#include "../platform/OverlappedIOContext.hpp"
#include "../platform/UDPSocketAsync.hpp"
#include "SecureChannel.hpp"
#include "KeyExchange.hpp"
#include <riftcompress.hpp>
#include <functional>
#include <memory>
#include <vector>

namespace RiftForged::Networking {

    class Connection {
    public:
        using SendCallback = std::function<void(const NetworkEndpoint&, const std::vector<uint8_t>&)>;

        Connection(const NetworkEndpoint& remoteAddr);

        void SetSendCallback(SendCallback cb);
        const NetworkEndpoint& GetRemoteAddress() const;

        void HandleRawPacket(const std::vector<uint8_t>& raw);
        void SendUnencrypted(const std::vector<uint8_t>& payload);
        void SendSecure(const std::vector<uint8_t>& payload);

        const KeyExchange::KeyBuffer& GetLocalPublicKey() const;
        void PerformKeyExchange(const KeyExchange::KeyBuffer& clientPubKey, bool isServer);

    private:
        NetworkEndpoint endpoint;
        bool handshakeComplete = false;

        SecureChannel secureChannel;
        KeyExchange keyExchange;

        uint64_t nonceRx = 1;
        uint64_t nonceTx = 1;

        SendCallback sendCallback;
    };

} // namespace RiftForged::Networking
