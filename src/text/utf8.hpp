#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace mod {

// When `valid` is false, `len == 1` and `cp` holds the raw byte value.
struct Decoded {
    char32_t cp = 0;
    std::uint8_t len = 0;
    bool valid = false;
};

// Decodes the unit at the start of `bytes` (which may be shorter than the sequence
// needs). Overlong forms, surrogates, values above U+10FFFF and truncated sequences
// are invalid. An empty span gives `len == 0`.
Decoded decode(std::span<const std::byte> bytes);

// The unit that ends exactly at the end of `bytes`; at most 4 bytes are inspected.
Decoded decode_before(std::span<const std::byte> bytes);

struct ClusterResult {
    enum Kind { found, need_more } kind = found;
    std::uint32_t length = 0;
};

// The most code points one cluster may hold for cursor movement and deletion. Each
// invalid byte counts as one code point.
inline constexpr std::uint32_t MAX_CLUSTER_CODE_POINTS = 32;

// Length of the UAX #29 extended grapheme cluster that starts the window.
ClusterResult next_grapheme_boundary(std::span<const std::byte> bytes, bool at_end);

// Length of the cluster that ends the window.
ClusterResult prev_grapheme_boundary(std::span<const std::byte> bytes, bool at_start);

// Width in cells at display column `column` with tab stops every `tab_width` (1 to 16).
int display_width(const Decoded& d, std::uint64_t column, int tab_width);

enum class CharClass { space, word, punct, newline };

CharClass char_class(const Decoded& d);

// UTF-16 code units in `bytes`; each invalid byte counts as 1.
std::uint64_t utf16_length(std::span<const std::byte> bytes);

// The display columns of a short UTF-8 text such as a label or a file name: a wide
// character two, a combining mark none, an invalid byte one (as the screen draws it).
int text_columns(std::string_view text);

}  // namespace mod
