#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

#include "text/piece_tree.hpp"

namespace mod {

// A line is laid out in independent blocks of this many bytes: a row never spans two
// blocks, so finding a row costs at most one block however long the line is.
inline constexpr std::uint64_t kWrapBlock = 4096;

// The byte length of the first row of `text` (one line, or the rest of one, with no
// line break) wrapped at `cols` columns: as many grapheme clusters as fit, cut back to
// just after the last whitespace when a word would be split; whitespace that does not
// fit stays at the end of the row. At least one cluster unless `text` is empty.
std::size_t wrap_row_length(std::string_view text, int cols, int tab_width);

// The soft-wrap layout of a document: every line as rows of at most `cols` columns.
// Stateless over the tree, so it is built where it is needed and never invalidated.
// A row is named by the offset of its first byte.
class WrapLayout {
public:
    static constexpr std::uint64_t npos = PieceTree::npos;

    WrapLayout(const PieceTree& text, int cols, int tab_width);

    int cols() const noexcept { return cols_; }
    // The start of the line containing `pos`, and the end of the line that starts at
    // `line_start` (before its LF or CR LF, or the end of the text).
    std::uint64_t line_start(std::uint64_t pos) const;
    std::uint64_t line_end(std::uint64_t line_start) const;
    // The row containing `pos`. A position where one row ends and the next begins
    // belongs to the next; the end of the line belongs to the line's last row.
    std::uint64_t row_start(std::uint64_t pos) const;
    // The end of the row that starts at `row_start`: the next row's start, or the line end.
    std::uint64_t row_end(std::uint64_t row_start) const;
    // The row after / before the one that starts at `row_start`, across lines; npos at
    // the last / first row of the text.
    std::uint64_t next_row(std::uint64_t row_start) const;
    std::uint64_t prev_row(std::uint64_t row_start) const;

private:
    std::uint64_t block_start(std::uint64_t line_start, std::uint64_t line_end, std::uint64_t index) const;
    std::uint64_t block_of(std::uint64_t line_start, std::uint64_t line_end, std::uint64_t pos) const;

    const PieceTree& text_;
    int cols_;
    int tab_width_;
};

}  // namespace mod
