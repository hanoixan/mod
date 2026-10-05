#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mod::fuzz {

// Takes a fuzz input apart: small numbers for choices, then byte strings.
class Reader {
public:
    Reader(const std::uint8_t* data, std::size_t size) : bytes_(data, size) {}

    bool empty() const noexcept { return pos_ >= bytes_.size(); }
    std::uint8_t byte() { return empty() ? 0 : bytes_[pos_++]; }
    // A number in [0, n), n > 0.
    std::size_t below(std::size_t n) { return byte() % n; }
    // Up to `max` bytes, the length chosen by the input.
    std::string_view text(std::size_t max) {
        const std::size_t want = std::min<std::size_t>(below(max + 1), bytes_.size() - std::min(pos_, bytes_.size()));
        const std::string_view out(reinterpret_cast<const char*>(bytes_.data()) + pos_, want);
        pos_ += want;
        return out;
    }
    std::string_view rest() {
        const std::size_t from = std::min(pos_, bytes_.size());
        pos_ = bytes_.size();
        return {reinterpret_cast<const char*>(bytes_.data()) + from, bytes_.size() - from};
    }

private:
    std::span<const std::uint8_t> bytes_;
    std::size_t pos_ = 0;
};

// Stops the fuzzer with a crash it reports: a property that must hold did not.
[[noreturn]] inline void fail() { __builtin_trap(); }

}  // namespace mod::fuzz
