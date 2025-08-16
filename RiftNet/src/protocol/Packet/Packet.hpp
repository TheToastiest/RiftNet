#pragma once

#include <cstdint>

// Ensure structs are packed without padding to match the wire format exactly.
#pragma pack(push, 1)

namespace RiftNet::Protocol {

    // =========================
    // Protocol Constants
    // =========================
    // These constants are used during the initial handshake to ensure that
    // the client and server are speaking the same language.
    constexpr uint32_t PROTOCOL_MAGIC = 0x52494654u; // 'RIFT'
    constexpr uint16_t PROTOCOL_VERSION = 0x0001;


    // =========================
    // Packet Type Definition
    // =========================
    // Enum for all high-level packet types. This is the very first byte after decryption.
    // It defines the purpose of the packet at the protocol level.
    enum class PacketType : uint8_t {
        // --- Connection Management ---
        // Used for the initial secure connection establishment.
        Handshake_Request,          // C->S: Initiates a connection.
        Handshake_Challenge,        // S->C: A challenge to prove ownership of the source address.
        Handshake_Response,         // C->S: The client's response to the server's challenge.
        Handshake_Verified,         // S->C: The server's final confirmation of a secure connection.
        Disconnect,                 // Either -> Either: Graceful disconnect notification.

        // --- Data Transfer ---
        // Used for sending application-level data.
        Data_Unreliable,            // For data that can be lost (e.g., player position).
        Data_Reliable,              // For data that must arrive (e.g., chat messages).

        // --- Keep-alive ---
        // Used to maintain the connection and detect timeouts.
        Heartbeat,                  // Either -> Either: "Are you still there?"
        Heartbeat_Ack,              // Either -> Either: "Yes, I'm here."
    };


    // =========================
    // Packet Header Structures
    // =========================

    // The header that appears at the start of EVERY decrypted packet.
    struct GeneralPacketHeader {
        PacketType Type;
    };

    // The header that ONLY follows a GeneralPacketHeader if the type is Data_Reliable.
    // This structure contains all the necessary information for the UDPReliabilityProtocol.
    struct ReliabilityPacketHeader {
        uint16_t sequence;          // Sequence number of this packet.
        uint16_t ack;               // The most recent sequence number received from the other side.
        uint32_t ack_bitfield;      // A bitfield representing acks for the 32 packets prior to `ack`.
    };

} // namespace RiftNet::Protocol

#pragma pack(pop)
