// Decoding, widths, character classes and grapheme clusters over any bytes, forwards and
// backwards: every step makes progress and stays inside the input.
#include <cstddef>
#include <cstdint>
#include <span>

#include "fuzz/fuzz_reader.hpp"
#include "text/utf8.hpp"

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
    const auto bytes = std::as_bytes(std::span(data, size));
    std::uint64_t column = 0;
    for (std::size_t i = 0; i < size;) {
        const mod::Decoded d = mod::decode(bytes.subspan(i));
        if (d.len == 0 || i + d.len > size) mod::fuzz::fail();
        column += static_cast<std::uint64_t>(mod::display_width(d, column, 4));
        (void)mod::char_class(d);
        i += d.len;
    }
    for (std::size_t i = size; i > 0;) {
        const mod::Decoded d = mod::decode_before(bytes.first(i));
        if (d.len == 0 || d.len > i) mod::fuzz::fail();
        i -= d.len;
    }
    for (std::size_t i = 0; i < size;) {
        const mod::ClusterResult c = mod::next_grapheme_boundary(bytes.subspan(i), true);
        if (c.length == 0 || i + c.length > size) mod::fuzz::fail();
        i += c.length;
    }
    for (std::size_t i = size; i > 0;) {
        const mod::ClusterResult c = mod::prev_grapheme_boundary(bytes.first(i), true);
        if (c.length == 0 || c.length > i) mod::fuzz::fail();
        i -= c.length;
    }
    (void)mod::utf16_length(bytes);
    return 0;
}
