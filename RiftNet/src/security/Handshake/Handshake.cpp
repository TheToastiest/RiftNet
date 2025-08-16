#include "pch.h"
#include "Handshake.hpp"

#include <array>
#include <algorithm>

namespace {
    constexpr std::array<uint8_t, 4> kMagic = { 'R','F','N','T' };
}

namespace RiftNet::Protocol::Handshake {

    std::vector<uint8_t> BuildHello(const byte_vec& pub32) {
        if (pub32.size() != 32) return {};
        std::vector<uint8_t> buf;
        buf.reserve(Hello::kSize);
        buf.insert(buf.end(), kMagic.begin(), kMagic.end());
        buf.push_back(Hello::kVersion);
        buf.push_back(Hello::kTypeHello);
        buf.insert(buf.end(), pub32.begin(), pub32.end());
        return buf;
    }

    bool TryParseHello(const uint8_t* data, uint32_t size, byte_vec& outPubKey) {
        if (!data || size != Hello::kSize) return false;
        if (!std::equal(kMagic.begin(), kMagic.end(), data)) return false;
        if (data[4] != Hello::kVersion) return false;
        if (data[5] != Hello::kTypeHello) return false;

        outPubKey.assign(data + 6, data + 6 + 32);
        return true;
    }

} // namespace RiftNet::Protocol::Handshake
