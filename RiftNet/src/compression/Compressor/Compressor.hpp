#pragma once

#include <riftcompress.hpp>
#include <memory>
#include <vector>

// Forward declare the Compressor class from the global namespace
class Compressor;

namespace RiftNet::Compression {

    /**
     * @class Compressor
     * @brief Manages the compression and decompression pipeline for a connection.
     * This is a stateful wrapper around the stateless riftcompress library.
     */
    class Compressor {
    public:
        Compressor();
        ~Compressor(); // Required for unique_ptr to incomplete type

        /**
         * @brief Compresses a block of data using the configured algorithm (LZ4 by default).
         * @param plainData The data to compress.
         * @return A vector containing the compressed data.
         */
        std::vector<uint8_t> Compress(const std::vector<uint8_t>& plainData);

        /**
         * @brief Decompresses a block of data.
         * @param compressedData The data to decompress.
         * @return A vector containing the original plaintext data. Returns an empty vector on failure.
         */
        std::vector<uint8_t> Decompress(const std::vector<uint8_t>& compressedData);

    private:
        // A unique_ptr to the Compressor class from your riftcompress library.
        std::unique_ptr<::Compressor> m_compressor;
    };

} // namespace RiftNet::Compression
