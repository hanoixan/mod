#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "util/error.hpp"

namespace mod {

// SHA-256 digest in standard big-endian output order, as `sha256sum` prints it.
using ContentHash = std::array<std::byte, 32>;

// 64 lowercase hex characters.
std::string to_hex(const ContentHash& hash);

// Streaming SHA-256 (FIPS 180-4). Not reusable after `finish`.
class ContentHasher {
public:
    void update(std::span<const std::byte> bytes);
    ContentHash finish();

private:
    void compress(const std::byte* block);

    std::array<std::uint32_t, 8> state_{
        0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u,
    };
    std::array<std::byte, 64> block_{};
    std::size_t block_len_ = 0;
    std::uint64_t bit_length_ = 0;
    bool finished_ = false;
};

// IEEE 802.3 CRC-32 (reflected 0xEDB88320, init and xorout 0xFFFFFFFF), as zlib and PNG.
// Chain by passing the previous result as `seed`.
std::uint32_t crc32(std::span<const std::byte> bytes, std::uint32_t seed = 0);

}  // namespace mod
