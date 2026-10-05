#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "edit/editor.hpp"
#include "syntax/markdown_render.hpp"

namespace mod {

// A Markdown document laid out for reading, with the cursor positions that map the
// layout back to the source: the start of each code point that shows source text, and
// the end of each line that has any. Main thread.
class ReadingLayout {
public:
    struct Spot {
        std::size_t line = 0;  // rendered line
        std::size_t byte = 0;  // in that line's text
        int col = 0;           // display column
    };

    ReadingLayout(std::string_view text, int width);

    const RenderedPage& page() const noexcept { return page_; }
    // The position at `offset`, else the first after it, else the last.
    Spot locate(std::uint64_t offset) const;
    // Where `motion` takes the cursor from `cursor`; `sticky` is the column Up and Down aim for.
    std::uint64_t move(Motion motion, std::uint64_t cursor, std::size_t page_lines, std::optional<int>& sticky) const;
    // The text shown for the source range [start, end).
    std::string visible_text(std::uint64_t start, std::uint64_t end) const;

private:
    struct Entry {
        std::uint64_t offset = 0;
        Spot spot;
        bool end = false;  // a line's end, after its last shown code point
    };
    std::size_t index_at(std::uint64_t offset) const;  // first entry at or after, clamped
    // The line nearest `target` with positions, searching towards `dir` first.
    std::optional<std::size_t> line_with_positions(std::size_t target, int dir) const;
    std::uint64_t on_line(std::size_t line, int col) const;

    RenderedPage page_;
    std::vector<Entry> entries_;                      // sorted by offset
    std::vector<std::vector<std::size_t>> by_line_;   // per rendered line: entries sorted by column
};

}  // namespace mod
