// include/riftnet/RiftNetWire.hpp
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <vector>


namespace RiftNet {

    // These map directly to your ReliablePacketHeader::packetType
    enum class PacketType : uint8_t {
        EchoTest = 1,   // you already use this
        Input = 2,   // C -> S
        Snapshot = 3,   // S -> C
        TimeSync = 4,   // S -> C  (server QPC)
    };

    // S->C: carry server QPC for offset estimation
#pragma pack(push,1)
    struct MsgTimeSync {
        uint64_t frame_idx;
        int64_t  server_qpc_ticks;  // QueryPerformanceCounter ticks at send
    };

    // S->C: minimal bench snapshot header; payload follows (0..N bytes)
    struct SnapshotHeader {
        uint64_t frame_idx;
        uint32_t entity_count; // optional
    };

    // C->S: example input packet; extend as needed
    struct InputPkt {
        uint64_t monotonic;
        float    ax, ay;
    };
#pragma pack(pop)

    // Small helper to prefix a 1-byte PacketType in front of a payload
    inline void WritePacket(std::vector<uint8_t>& out, PacketType t,
        const void* body, size_t len)
    {
        out.resize(1 + len);
        out[0] = static_cast<uint8_t>(t);
        if (len) std::memcpy(out.data() + 1, body, len);
    }



} // namespace RiftNet
