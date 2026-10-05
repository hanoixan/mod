#include "text/wrap.hpp"

#include <algorithm>
#include <span>
#include <string>

#include "text/utf8.hpp"

namespace mod {
namespace {

struct Cluster {
    std::size_t len = 0;
    bool space = false;
};

// The grapheme cluster at the start of `bytes` (never empty), and its width at `col`.
Cluster cluster_at(std::span<const std::byte> bytes, std::uint64_t col, int tab_width, int& width) {
    const ClusterResult r = next_grapheme_boundary(bytes, true);
    Cluster c;
    c.len = std::clamp<std::size_t>(r.length, 1, bytes.size());
    width = 0;
    for (std::size_t i = 0; i < c.len;) {
        const Decoded d = decode(bytes.subspan(i, c.len - i));
        if (i == 0) c.space = char_class(d) == CharClass::space;
        width += std::max(0, display_width(d, col + static_cast<std::uint64_t>(width), tab_width));
        i += std::max<std::size_t>(1, d.len);
    }
    return c;
}

}  // namespace

std::size_t wrap_row_length(std::string_view text, int cols, int tab_width) {
    const auto bytes = std::as_bytes(std::span(text.data(), text.size()));
    const int limit = std::max(1, cols);
    std::size_t i = 0;
    std::size_t last_break = 0;  // just after the last whitespace cluster that fitted
    int col = 0;
    while (i < bytes.size()) {
        int w = 0;
        const Cluster c = cluster_at(bytes.subspan(i), static_cast<std::uint64_t>(col), tab_width, w);
        if (col + w > limit && i > 0) {
            if (c.space) {
                // Whitespace that does not fit hangs at the end of this row.
                while (i < bytes.size()) {
                    int ignored = 0;
                    const Cluster s = cluster_at(bytes.subspan(i), 0, tab_width, ignored);
                    if (!s.space) break;
                    i += s.len;
                }
                return i;
            }
            return last_break > 0 ? last_break : i;
        }
        col += w;
        i += c.len;
        if (c.space) last_break = i;
    }
    return i;
}

WrapLayout::WrapLayout(const PieceTree& text, int cols, int tab_width)
    : text_(text), cols_(std::max(1, cols)), tab_width_(std::clamp(tab_width, 1, 16)) {}

std::uint64_t WrapLayout::line_start(std::uint64_t pos) const {
    pos = std::min(pos, text_.size());
    if (pos == 0) return 0;
    const std::uint64_t lf = text_.find_lf_backward(pos, pos);
    return lf == npos ? 0 : lf + 1;
}

std::uint64_t WrapLayout::line_end(std::uint64_t line_start) const {
    const std::uint64_t size = text_.size();
    if (line_start >= size) return size;
    const std::uint64_t lf = text_.find_lf_forward(line_start, size - line_start);
    if (lf == npos) return size;
    if (lf > line_start && text_.byte_at(lf - 1) == std::byte{'\r'}) return lf - 1;  // CR before LF is hidden
    return lf;
}

// Block `index` starts at the first byte at or after `line_start + index * kWrapBlock`
// that does not continue a UTF-8 sequence, so a block never begins inside a character.
std::uint64_t WrapLayout::block_start(std::uint64_t line_start, std::uint64_t line_end, std::uint64_t index) const {
    if (index == 0) return line_start;
    std::uint64_t x = line_start + index * kWrapBlock;
    if (x >= line_end) return line_end;
    for (int skipped = 0; skipped < 3 && x < line_end && (std::to_integer<unsigned>(text_.byte_at(x)) & 0xC0) == 0x80; ++skipped) ++x;
    return x;
}

std::uint64_t WrapLayout::block_of(std::uint64_t line_start, std::uint64_t line_end, std::uint64_t pos) const {
    std::uint64_t k = (pos - line_start) / kWrapBlock;
    if (k > 0 && pos < block_start(line_start, line_end, k)) --k;
    return k;
}

std::uint64_t WrapLayout::row_end(std::uint64_t row_start) const {
    const std::uint64_t ls = line_start(row_start);
    const std::uint64_t le = line_end(ls);
    if (row_start >= le) return le;
    const std::uint64_t block_end = block_start(ls, le, block_of(ls, le, row_start) + 1);
    const std::string text = text_.read(row_start, block_end - row_start);
    return row_start + std::max<std::size_t>(1, wrap_row_length(text, cols_, tab_width_));
}

std::uint64_t WrapLayout::row_start(std::uint64_t pos) const {
    const std::uint64_t ls = line_start(pos);
    const std::uint64_t le = line_end(ls);
    if (ls == le) return ls;
    pos = std::min(pos, le);
    // The line end belongs to the last row, which is in the block of the line's last byte.
    const std::uint64_t k = block_of(ls, le, pos == le ? le - 1 : pos);
    const std::uint64_t begin = block_start(ls, le, k);
    const std::uint64_t block_end = block_start(ls, le, k + 1);
    const std::string text = text_.read(begin, block_end - begin);
    std::uint64_t row = begin;
    for (;;) {
        const std::string_view rest = std::string_view(text).substr(static_cast<std::size_t>(row - begin));
        const std::uint64_t end = row + std::max<std::size_t>(1, wrap_row_length(rest, cols_, tab_width_));
        if (pos < end || end >= block_end) return row;
        row = end;
    }
}

std::uint64_t WrapLayout::next_row(std::uint64_t row_start) const {
    const std::uint64_t ls = line_start(row_start);
    const std::uint64_t le = line_end(ls);
    const std::uint64_t end = row_end(row_start);
    if (end < le) return end;
    const std::uint64_t size = text_.size();
    if (le >= size) return npos;
    const std::uint64_t lf = text_.find_lf_forward(le, size - le);
    return lf == npos ? npos : lf + 1;
}

std::uint64_t WrapLayout::prev_row(std::uint64_t row_start) const {
    const std::uint64_t ls = line_start(row_start);
    if (row_start > ls) return this->row_start(row_start - 1);
    if (ls == 0) return npos;
    const std::uint64_t prev_line = line_start(ls - 1);
    return this->row_start(line_end(prev_line));
}

}  // namespace mod
