#include "ui/screen.hpp"

#include <algorithm>
#include <cstring>
#include <optional>
#include <span>

#include "text/utf8.hpp"

namespace mod {

void Screen::resize(TerminalSize size) {
    rows_ = std::max(1, size.rows);
    cols_ = std::max(1, size.cols);
    const auto n = static_cast<std::size_t>(rows_) * static_cast<std::size_t>(cols_);
    back_.assign(n, Cell{});
    front_.assign(n, Cell{});
    invalidate();
}

namespace {

// C0 controls, DEL and C1 controls: bytes a terminal would act on rather than show.
bool is_control(char32_t cp) { return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F); }

// A control, or a byte that is not UTF-8: a lone 0x80-0x9F byte is a C1 control to a
// terminal reading 8-bit controls.
bool unsafe_to_draw(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const Decoded d = decode(std::as_bytes(std::span(text.data() + i, text.size() - i)));
        if (!d.valid || is_control(d.cp)) return true;
        i += std::max<std::size_t>(1, d.len);
    }
    return false;
}

}  // namespace

int Screen::put(int row, int col, std::string_view text, int width, Attr attr) {
    if (row < 0 || row >= rows_ || col < 0 || col > cols_) return 0;
    // Everything drawn passes through here, so this is where text from files, file names
    // and messages is kept from reaching the terminal as a control sequence.
    if (unsafe_to_draw(text)) {
        text = "?";
        width = 1;
    }
    width = std::clamp(width, 0, 2);
    if (width == 0) {
        // A zero-width code point joins the cell before it, as far as the cell has room.
        int c = col - 1;
        while (c > 0 && back_[index(row, c)].width == 0) --c;
        if (c < 0) return 0;
        Cell& cell = back_[index(row, c)];
        if (cell.len + text.size() <= cell.utf8.size()) {
            std::memcpy(cell.utf8.data() + cell.len, text.data(), text.size());
            cell.len = static_cast<std::uint8_t>(cell.len + text.size());
        }
        return 0;
    }
    if (col >= cols_) return 0;
    Cell& cell = back_[index(row, col)];
    // Overwriting half of a wide character blanks its other half.
    if (cell.width == 0 && col > 0) back_[index(row, col - 1)] = Cell{{' '}, 1, 1, back_[index(row, col - 1)].attr};
    if (cell.width == 2 && col + 1 < cols_) back_[index(row, col + 1)] = Cell{{' '}, 1, 1, attr};
    if (width == 2 && col + 1 >= cols_) {
        // A wide character in the last column would make the terminal wrap.
        cell = Cell{};
        cell.attr = attr;
        return 1;
    }
    cell = Cell{};
    const std::size_t n = std::min(text.size(), cell.utf8.size());
    if (n == 0 || text.size() > cell.utf8.size()) {
        // Longer than a cell holds: keep the base character only.
        const Decoded d = decode(std::as_bytes(std::span(text.data(), text.size())));
        const std::size_t base = std::max<std::size_t>(1, d.len);
        std::memcpy(cell.utf8.data(), text.data(), std::min(base, text.size()));
        cell.len = static_cast<std::uint8_t>(std::min(base, text.size()));
        if (cell.len == 0) {
            cell.utf8[0] = ' ';
            cell.len = 1;
        }
    } else {
        std::memcpy(cell.utf8.data(), text.data(), n);
        cell.len = static_cast<std::uint8_t>(n);
    }
    cell.width = static_cast<std::uint8_t>(width);
    cell.attr = attr;
    if (width == 2) {
        Cell& next = back_[index(row, col + 1)];
        if (next.width == 2 && col + 2 < cols_) back_[index(row, col + 2)] = Cell{{' '}, 1, 1, attr};
        next = Cell{};
        next.len = 0;
        next.width = 0;
        next.attr = attr;
    }
    return width;
}

int Screen::print(int row, int col, int col_end, std::string_view text, Attr attr) {
    col_end = std::min(col_end, cols_);
    std::size_t i = 0;
    while (i < text.size() && col < col_end) {
        const auto rest = std::as_bytes(std::span(text.data() + i, text.size() - i));
        const Decoded d = decode(rest);
        const std::size_t len = std::max<std::size_t>(1, d.len);
        if (!d.valid) {
            put(row, col++, "?", 1, attr);
            i += len;
            continue;
        }
        const int w = display_width(d, static_cast<std::uint64_t>(col), 1);
        if (d.cp == 0x2026 && vt100_text()) {
            for (int k = 0; k < 3 && col < col_end; ++k) put(row, col++, ".", 1, attr);  // a VT100 has no '…'
        } else if (d.cp < 0x20 || d.cp == 0x7F) {
            put(row, col++, " ", 1, attr);
        } else if (w == 0) {
            put(row, col, text.substr(i, len), 0, attr);
        } else if (col + w > col_end) {
            break;
        } else {
            col += put(row, col, text.substr(i, len), w, attr);
        }
        i += len;
    }
    return col;
}

void Screen::add_flags(Rect area, std::uint8_t flags) {
    for (int r = std::max(0, area.row); r < std::min(rows_, area.row + area.rows); ++r) {
        for (int c = std::max(0, area.col); c < std::min(cols_, area.col + area.cols); ++c) back_[index(r, c)].attr.flags |= flags;
    }
}

void Screen::fill(int row, int col_from, int col_to, Attr attr) {
    if (row < 0 || row >= rows_) return;
    col_from = std::max(0, col_from);
    col_to = std::min(cols_, col_to);
    for (int c = col_from; c < col_to; ++c) put(row, c, " ", 1, attr);
}

void Screen::set_cursor(int row, int col, bool visible) {
    cursor_row_ = row;
    cursor_col_ = col;
    cursor_visible_ = visible;
}

void Screen::invalidate() { front_valid_ = false; }

Status Screen::flush() {
    out_.clear();
    out_ += output_->begin_frame();
    const bool full = !front_valid_;
    if (full) out_ += output_->clear_screen();
    if (full || sent_style_ != cursor_style_) {
        out_ += output_->cursor_shape(cursor_style_);
        sent_style_ = cursor_style_;
    }
    std::optional<Attr> current;
    bool line_drawing = false;  // the VT100's line-drawing set is selected; end_frame deselects it
    int at_row = -1;
    int at_col = -1;
    for (int r = 0; r < rows_; ++r) {
        for (int c = 0; c < cols_; ++c) {
            const Cell& b = back_[index(r, c)];
            if (!full && b == front_[index(r, c)]) continue;
            if (b.width == 0) continue;  // drawn with the wide cell before it
            if (at_row != r || at_col != c) output_->append_move(out_, r, c);
            if (!current || !(*current == b.attr)) {
                output_->append_attr(out_, b.attr);
                current = b.attr;
            }
            output_->append_cell(out_, std::string_view(b.utf8.data(), b.len), line_drawing);
            at_row = r;
            at_col = c + b.width;
        }
    }
    out_ += output_->end_frame();
    if (cursor_visible_ && cursor_row_ >= 0 && cursor_row_ < rows_ && cursor_col_ >= 0 && cursor_col_ < cols_) {
        output_->append_move(out_, cursor_row_, cursor_col_);
        out_ += output_->show_cursor();
    }
    out_ += output_->end_sync();
    front_ = back_;
    front_valid_ = true;
    return terminal_.write(std::as_bytes(std::span(out_.data(), out_.size())));
}

}  // namespace mod
