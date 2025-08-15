// File: Connection.hpp

#pragma once

#include "Logger.hpp"
#include "protocols.hpp"
#include "NetworkEndpoint.hpp"
#include "UDPReliabilityProtocol.hpp"
#include "SecureChannel.hpp"
#include "KeyExchange.hpp"
#include <riftcompress.hpp>
#include <functional>
#include <memory>
#include <vector>
#include <unordered_map>

namespace RiftNet::Networking {


    class Connection {
    public:
        using AppPacketCallback = std::function<void(const std::string& key,
            uint8_t pktType,
            const uint8_t* body,
            size_t len)>;

        using SendCallback = std::function<void(const NetworkEndpoint&, const std::vector<uint8_t>&)>;
        void SetAppPacketCallback(AppPacketCallback cb);

        Connection(const NetworkEndpoint& remoteAddr, bool isServerRole);

        void SetSendCallback(SendCallback cb);
        const NetworkEndpoint& GetRemoteAddress() const;

        void HandleRawPacket(const std::vector<uint8_t>& raw);
        void SendUnencrypted(const std::vector<uint8_t>& payload);
        void SendSecure(const std::vector<uint8_t>& payload);
        void SendReliable(const std::vector<uint8_t>& plainData, uint8_t packetType);
        void SendRawPacket(const std::vector<uint8_t>& packet);
        void SendPacket(const std::vector<uint8_t>& reliablePayload, uint8_t packetType);
        bool IsConnected() const;
        void SendFramed(const std::vector<uint8_t>& framedWire);

        const KeyExchange::KeyBuffer& GetLocalPublicKey() const;
        void PerformKeyExchange(const KeyExchange::KeyBuffer& clientPubKey, bool isServer);

        ReliableConnectionState& GetReliableState();

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
        uint64_t nonceRx = 1;
        uint64_t nonceTx = 1;
        bool isServerRole_ = true;
        AppPacketCallback appCallback;
    };

} // namespace RiftForged::Networking
