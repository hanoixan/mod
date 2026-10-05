#include "util/hash.hpp"

#include <cassert>

namespace mod {
namespace {

constexpr std::array<std::uint32_t, 64> kRound = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

constexpr std::uint32_t rotr(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

std::uint32_t load_be32(const std::byte* p) {
    return (std::uint32_t(p[0]) << 24) | (std::uint32_t(p[1]) << 16) | (std::uint32_t(p[2]) << 8) |
           std::uint32_t(p[3]);
}

// Slicing-by-8: table[0] is the classic byte table, table[k] advances k more zero bytes.
constexpr std::array<std::array<std::uint32_t, 256>, 8> make_crc_tables() {
    std::array<std::array<std::uint32_t, 256>, 8> t{};
    for (std::uint32_t i = 0; i < 256; ++i) {
        std::uint32_t c = i;
        for (int k = 0; k < 8; ++k) c = (c & 1u) ? (c >> 1) ^ 0xEDB88320u : c >> 1;
        t[0][i] = c;
    }
    for (std::size_t i = 0; i < 256; ++i)
        for (std::size_t s = 1; s < 8; ++s) t[s][i] = (t[s - 1][i] >> 8) ^ t[0][t[s - 1][i] & 0xFFu];
    return t;
}

constexpr auto kCrc = make_crc_tables();

}  // namespace

std::string to_hex(const ContentHash& hash) {
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(64);
    for (std::byte b : hash) {
        const auto v = std::to_integer<unsigned>(b);
        out.push_back(digits[v >> 4]);
        out.push_back(digits[v & 0xFu]);
    }
    return out;
}

void ContentHasher::compress(const std::byte* block) {
    std::array<std::uint32_t, 64> w{};
    for (std::size_t i = 0; i < 16; ++i) w[i] = load_be32(block + 4 * i);
    for (std::size_t i = 16; i < 64; ++i) {
        const std::uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        const std::uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    std::uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (std::size_t i = 0; i < 64; ++i) {
        const std::uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const std::uint32_t ch = (e & f) ^ (~e & g);
        const std::uint32_t t1 = h + s1 + ch + kRound[i] + w[i];
        const std::uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t t2 = s0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void ContentHasher::update(std::span<const std::byte> bytes) {
    assert(!finished_);
    bit_length_ += std::uint64_t(bytes.size()) * 8;
    std::size_t i = 0;
    if (block_len_ > 0) {
        while (i < bytes.size() && block_len_ < 64) block_[block_len_++] = bytes[i++];
        if (block_len_ < 64) return;
        compress(block_.data());
        block_len_ = 0;
    }
    for (; i + 64 <= bytes.size(); i += 64) compress(bytes.data() + i);
    while (i < bytes.size()) block_[block_len_++] = bytes[i++];
}

ContentHash ContentHasher::finish() {
    assert(!finished_);
    finished_ = true;
    const std::uint64_t bits = bit_length_;
    block_[block_len_++] = std::byte{0x80};
    if (block_len_ > 56) {
        while (block_len_ < 64) block_[block_len_++] = std::byte{0};
        compress(block_.data());
        block_len_ = 0;
    }
    while (block_len_ < 56) block_[block_len_++] = std::byte{0};
    for (int k = 7; k >= 0; --k) block_[block_len_++] = std::byte(static_cast<unsigned char>(bits >> (8 * k)));
    compress(block_.data());
    ContentHash out{};
    for (std::size_t i = 0; i < 8; ++i) {
        out[4 * i] = std::byte(static_cast<unsigned char>(state_[i] >> 24));
        out[4 * i + 1] = std::byte(static_cast<unsigned char>(state_[i] >> 16));
        out[4 * i + 2] = std::byte(static_cast<unsigned char>(state_[i] >> 8));
        out[4 * i + 3] = std::byte(static_cast<unsigned char>(state_[i]));
    }
    return out;
}

std::uint32_t crc32(std::span<const std::byte> bytes, std::uint32_t seed) {
    std::uint32_t c = ~seed;
    const std::byte* p = bytes.data();
    std::size_t n = bytes.size();
    auto u = [](std::byte b) { return std::to_integer<std::uint32_t>(b); };
    while (n >= 8) {
        // Reflected CRC: the first four bytes are combined little-endian with the register.
        const std::uint32_t lo = c ^ (u(p[0]) | (u(p[1]) << 8) | (u(p[2]) << 16) | (u(p[3]) << 24));
        c = kCrc[7][lo & 0xFFu] ^ kCrc[6][(lo >> 8) & 0xFFu] ^ kCrc[5][(lo >> 16) & 0xFFu] ^ kCrc[4][lo >> 24] ^
            kCrc[3][u(p[4])] ^ kCrc[2][u(p[5])] ^ kCrc[1][u(p[6])] ^ kCrc[0][u(p[7])];
        p += 8;
        n -= 8;
    }
    while (n-- > 0) c = (c >> 8) ^ kCrc[0][(c ^ u(*p++)) & 0xFFu];
    return ~c;
}

}  // namespace mod
