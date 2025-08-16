#include "pch.h"
#include "Compressor.hpp"
#include "../../../utilities/logger/Logger.hpp"

#include <exception>
#include <string>
#include <vector>

namespace RiftNet::Compression {

    Compressor::Compressor() {
        try {
            // Default to LZ4 for real-time-friendly speed.
            auto lz4_algo = std::make_unique<LZ4Algorithm>();
            m_compressor = std::make_unique<::Compressor>(std::move(lz4_algo));
            RF_NETWORK_DEBUG("Compressor initialized with LZ4Algorithm");
        }
        catch (const std::exception& e) {
            RF_NETWORK_CRITICAL("Compressor::Compressor init failed: {}", e.what());
            m_compressor = nullptr;
        }
        catch (...) {
            RF_NETWORK_CRITICAL("Compressor::Compressor init failed: unknown exception");
            m_compressor = nullptr;
        }
    }

    Compressor::~Compressor() = default;

    std::vector<uint8_t> Compressor::Compress(const std::vector<uint8_t>& plainData) {
        if (!m_compressor) {
            RF_NETWORK_WARN("Compressor::Compress called but compressor not initialized ({} bytes input)", plainData.size());
            return {};
        }
        try {
            auto out = m_compressor->compress(plainData);
            RF_NETWORK_TRACE("Compress ok: in={} bytes, out={} bytes", plainData.size(), out.size());
            return out;
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Compressor::Compress threw: {}", e.what());
            return {};
        }
        catch (...) {
            RF_NETWORK_ERROR("Compressor::Compress threw: unknown exception");
            return {};
        }
    }

    std::vector<uint8_t> Compressor::Decompress(const std::vector<uint8_t>& compressedData) {
        if (!m_compressor) {
            RF_NETWORK_WARN("Compressor::Decompress called but compressor not initialized ({} bytes input)", compressedData.size());
            return {};
        }
        try {
            auto out = m_compressor->decompress(compressedData);
            RF_NETWORK_TRACE("Decompress ok: in={} bytes, out={} bytes", compressedData.size(), out.size());
            return out;
        }
        catch (const std::exception& e) {
            RF_NETWORK_ERROR("Compressor::Decompress threw: {}", e.what());
            return {};
        }
        catch (...) {
            RF_NETWORK_ERROR("Compressor::Decompress threw: unknown exception");
            return {};
        }
    }

} // namespace RiftNet::Compression
