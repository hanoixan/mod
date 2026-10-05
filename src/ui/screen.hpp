#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "platform/terminal.hpp"
#include "platform/terminal_output.hpp"
#include "util/error.hpp"

namespace mod {

inline constexpr std::uint8_t kDefaultColor = 255;

// Attr::flags bits.
inline constexpr std::uint8_t kBold = 1u << 0;
inline constexpr std::uint8_t kDim = 1u << 1;
inline constexpr std::uint8_t kItalic = 1u << 2;
inline constexpr std::uint8_t kUnderline = 1u << 3;
inline constexpr std::uint8_t kReverse = 1u << 4;
inline constexpr std::uint8_t kStrike = 1u << 5;

// `fg` and `bg` are ANSI colors 0–15, or kDefaultColor.
struct Attr {
    std::uint8_t fg = kDefaultColor;
    std::uint8_t bg = kDefaultColor;
    std::uint8_t flags = 0;

    friend bool operator==(const Attr&, const Attr&) = default;
};

struct Rect {
    int row = 0;
    int col = 0;
    int rows = 0;
    int cols = 0;
};

// A width-2 cell is followed by a continuation cell with `width == 0`.
struct Cell {
    std::array<char, 8> utf8{' '};
    std::uint8_t len = 1;
    std::uint8_t width = 1;
    Attr attr;

    friend bool operator==(const Cell&, const Cell&) = default;
};

// A double-buffered cell grid that emits only the changed cells.
class Screen {
public:
    explicit Screen(Terminal& terminal) : terminal_(terminal), output_(&output_for(TerminalMode::xterm)) {}

    // The escape codes frames are written with (xterm until set); redraws everything.
    void set_output(const TerminalOutput& output) {
        output_ = &output;
        invalidate();
    }

    void resize(TerminalSize size);
    int rows() const noexcept { return rows_; }
    int cols() const noexcept { return cols_; }

    // Returns the columns consumed; 0 when clipped.
    int put(int row, int col, std::string_view text, int width, Attr attr);
    // Draws `text` from `col`, at most up to `col_end`; returns the column after it.
    int print(int row, int col, int col_end, std::string_view text, Attr attr);
    // Adds `flags` (such as kDim) to the cells of `area` already drawn, clipped to the screen.
    void add_flags(Rect area, std::uint8_t flags);
    void fill(int row, int col_from, int col_to, Attr attr);
    void set_cursor(int row, int col, bool visible);
    // The cursor's shape, sent with the next frame when it changes (and after a full redraw).
    void set_cursor_style(CursorStyle style) { cursor_style_ = style; }
    Status flush();
    void invalidate();

    // For tests: where the cursor will be after the next flush, and the back buffer's cell.
    int cursor_row() const noexcept { return cursor_row_; }
    int cursor_col() const noexcept { return cursor_col_; }
    bool cursor_visible() const noexcept { return cursor_visible_; }
    const Cell& cell(int row, int col) const { return back_[index(row, col)]; }

private:
    std::size_t index(int row, int col) const {
        return static_cast<std::size_t>(row) * static_cast<std::size_t>(cols_) + static_cast<std::size_t>(col);
    }

    Terminal& terminal_;
    const TerminalOutput* output_;
    CursorStyle cursor_style_ = CursorStyle::bar;
    std::optional<CursorStyle> sent_style_;  // what the terminal shows, as far as we know
    int rows_ = 0;
    int cols_ = 0;
    std::vector<Cell> front_;
    std::vector<Cell> back_;
    bool front_valid_ = false;
    int cursor_row_ = 0;
    int cursor_col_ = 0;
    bool cursor_visible_ = false;
    std::string out_;
};

}  // namespace mod
