// File: Connection.hpp

#pragma once

#include "Logger.hpp"
#include "NetworkTypes.hpp"
#include "NetworkEndpoint.hpp"
#include "HandshakePacket.hpp"
#include "ReliableConnectionState.hpp"
//#include "ReliablePacketHeader.hpp"
#include "UDPReliabilityProtocol.hpp"
#include "PacketTypes.hpp"
#include "../platform/OverlappedIOContext.hpp"
#include "../platform/UDPSocketAsync.hpp"
#include "ReliableTypes.hpp"
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
        void SendReliable(const std::vector<uint8_t>& plainData, uint8_t packetType);
        void SendRawPacket(const std::vector<uint8_t>& packet);

        const KeyExchange::KeyBuffer& GetLocalPublicKey() const;
        void PerformKeyExchange(const KeyExchange::KeyBuffer& clientPubKey, bool isServer);

        uint64_t GenerateUniqueNonce();

        uint64_t encryptNonceBase = 0;

    private:
        NetworkEndpoint endpoint;
        bool handshakeComplete = false;
        ReliableConnectionState reliabilityState;

        SecureChannel secureChannel;
        std::unique_ptr<Encryptor> encryptor;
        std::unique_ptr<Compressor> compressor;
        KeyExchange keyExchange;

        uint16_t localSequence = 0;
        uint16_t remoteSequence = 0;
        uint32_t remoteAckBitfield = 0;
        uint64_t currentNonce = 0;

        SendCallback sendCallback;
        std::unordered_map<uint16_t, ReliablePacket> reliabilityMap;
        uint64_t nonceRx = 1;
        uint64_t nonceTx = 1;
    };

} // namespace RiftForged::Networking
