#pragma once

#include <cstdint>
#include <vector>
#include "riftencrypt.hpp"  // for byte_vec alias

namespace RiftNet::Protocol::Handshake {

    // Cleartext HELLO wire format:
    //
    // [0..3]  = 'R','F','N','T'
    // [4]     = version (1)
    // [5]     = msg type (0x01 = HELLO)
    // [6..37] = 32-byte X25519 public key
    //
    // Total size = 38 bytes
    struct Hello {
        static constexpr uint8_t  kVersion = 1;
        static constexpr uint8_t  kTypeHello = 0x01;
        static constexpr uint32_t kSize = 38;
    };

    // Build a HELLO frame with our 32-byte public key.
    std::vector<uint8_t> BuildHello(const byte_vec& pub32);

    // If the buffer is a valid HELLO, fill outPubKey (32 bytes) and return true.
    bool TryParseHello(const uint8_t* data, uint32_t size, byte_vec& outPubKey);

} // namespace RiftNet::Protocol::Handshake
