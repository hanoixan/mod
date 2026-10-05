#include "ui/editor_view.hpp"

#include <algorithm>
#include <format>
#include <span>
#include <tuple>

#include "text/utf8.hpp"
#include "text/wrap.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {


constexpr std::uint64_t kReadSlice = 64 * 1024;  // bytes of a line read at a time
constexpr std::uint64_t npos = PieceTree::npos;

int digits(std::uint64_t n) {
    int d = 1;
    while (n >= 10) {
        n /= 10;
        ++d;
    }
    return d;
}

std::string_view history_label(HistoryState s) {
    switch (s) {
        case HistoryState::session_only: return "";  // kept in memory: the default, not news
        case HistoryState::verifying: return "history: verifying";
        case HistoryState::attached: return "";
        case HistoryState::read_only: return "history: read-only";
        case HistoryState::disabled: return "history: off";
    }
    return "";
}

Attr overlay(Attr base, Style s) {
    const Attr o = attr_for(s);
    if (s == Style::selection) {
        base.flags |= o.flags;
        return base;
    }
    return o;
}

// A row past the end of the text: `~` at the right of the gutter, when it has room.
void draw_past_end(Screen& screen, int row, int col, int gutter) {
    if (gutter < 2) return;
    screen.print(row, col, col + gutter, std::string(static_cast<std::size_t>(gutter - 2), ' ') + "~", on_page(attr_for(Style::gutter)));
}

}  // namespace

EditorView::EditorView(Document& doc, const Editor& editor, Highlighter* highlighter, int tab_width)
    : doc_(doc), editor_(editor), highlighter_(highlighter), tab_width_(std::clamp(tab_width, 1, 16)) {
    doc_.add_listener(this);
}

EditorView::~EditorView() { doc_.remove_listener(this); }

void EditorView::set_tab_width(int width) { tab_width_ = std::clamp(width, 1, 16); }

void EditorView::set_word_wrap(bool on) {
    if (on == wrap_) return;
    wrap_ = on;
    hscroll_ = 0;
    if (!wrap_) top_ = line_start_of(top_);  // one row per line again
}

void EditorView::set_top(std::uint64_t offset) {
    // A row start under word wrap is found again by the next scroll or frame.
    top_ = wrap_ ? std::min(offset, doc_.text().size()) : line_start_of(offset);
    if (layout_) rtop_ = layout_->locate(offset).line;
}

void EditorView::set_reading(bool on) {
    if (reading_ == on) return;
    reading_ = on;
    reading_sticky_.reset();
    rhscroll_ = 0;
    if (!on) layout_.reset();
}

// The renderer needs 20 columns; one more keeps a cell for the cursor at a line's end.
const ReadingLayout* EditorView::layout_for(int area_cols) {
    const int gutter = std::min(gutter_width(), std::max(0, area_cols - 1));
    const int width = area_cols - gutter - 1;
    if (!reading_ || preview_ || width < 20) {
        layout_.reset();
        return nullptr;
    }
    if (!layout_ || layout_version_ != doc_.version() || layout_width_ != width) {
        const bool had = layout_ != nullptr;
        const std::uint64_t top_src = had ? 0 : top_;
        const PieceTree& t = doc_.text();
        layout_ = std::make_unique<ReadingLayout>(t.read(0, t.size()), width);
        layout_version_ = doc_.version();
        layout_width_ = width;
        if (!had) rtop_ = layout_->locate(top_src).line;
        rtop_ = std::min(rtop_, layout_->page().lines.empty() ? 0 : layout_->page().lines.size() - 1);
    }
    return layout_.get();
}

int EditorView::wrap_cols(int area_cols) const {
    const int gutter = std::min(gutter_width(), std::max(0, area_cols - 1));
    return std::max(1, area_cols - gutter - 1);
}

std::uint64_t EditorView::line_start_of(std::uint64_t pos) const {
    const PieceTree& t = doc_.text();
    pos = std::min(pos, t.size());
    if (pos == 0) return 0;
    // On the line last measured, only the bytes past what is known need looking at. With
    // word wrap the index may be for a row that starts mid-line, which says nothing here.
    const ColumnIndex& ix = columns_;
    const bool at_line_start = ix.line_start == 0 || (ix.line_start <= t.size() && t.byte_at(ix.line_start - 1) == std::byte{'\n'});
    if (ix.valid && ix.version == doc_.version() && ix.size == t.size() && pos >= ix.line_start && at_line_start) {
        if (pos <= ix.measured_to || t.find_lf_backward(pos, pos - ix.measured_to) == npos) return ix.line_start;
    }
    const std::uint64_t lf = t.find_lf_backward(pos, pos);
    return lf == npos ? 0 : lf + 1;
}

std::uint64_t EditorView::next_line(std::uint64_t line_start) const {
    const PieceTree& t = doc_.text();
    if (line_start >= t.size()) return npos;
    const std::uint64_t lf = t.find_lf_forward(line_start, t.size() - line_start);
    return lf == npos ? npos : lf + 1;
}

bool EditorView::ColumnIndex::holds(const Document& doc, int tab, std::uint64_t line) const {
    return valid && version == doc.version() && size == doc.text().size() && tab_width == tab && line_start == line;
}

EditorView::ColumnIndex& EditorView::column_index(std::uint64_t line_start) const {
    if (!columns_.holds(doc_, tab_width_, line_start)) columns_ = ColumnIndex{true, doc_.version(), doc_.text().size(), tab_width_, line_start, {{line_start, 0}}, line_start};
    return columns_;
}

std::pair<std::uint64_t, std::uint64_t> EditorView::column_start(std::uint64_t line_start, std::uint64_t col) const {
    // Only looks: drawing the other lines must not throw away the cursor line's checkpoints.
    const ColumnIndex& ix = columns_;
    if (!ix.holds(doc_, tab_width_, line_start)) return {line_start, 0};
    const auto after = std::upper_bound(ix.marks.begin(), ix.marks.end(), col, [](std::uint64_t c, const auto& m) { return c < m.second; });
    return *std::prev(after);
}

std::uint64_t EditorView::column_of(std::uint64_t line_start, std::uint64_t pos) const {
    const PieceTree& t = doc_.text();
    ColumnIndex& ix = column_index(line_start);
    const auto after = std::upper_bound(ix.marks.begin(), ix.marks.end(), pos, [](std::uint64_t p, const auto& m) { return p < m.first; });
    std::uint64_t off = std::prev(after)->first;
    std::uint64_t col = std::prev(after)->second;
    ix.measured_to = std::max(ix.measured_to, pos);
    std::string buf;
    while (off < pos) {
        const std::uint64_t n = std::min(kReadSlice, pos - off);
        buf = t.read(off, std::min(n + 3, t.size() - off));  // a little extra so a unit is never cut
        std::size_t i = 0;
        while (i < n) {
            if (off + i >= ix.marks.back().first + kColumnMarkEvery) ix.marks.emplace_back(off + i, col);
            const Decoded d = decode(std::as_bytes(std::span(buf.data() + i, buf.size() - i)));
            if (d.valid && d.cp == '\r' && off + i + 1 < t.size() && t.byte_at(off + i + 1) == std::byte{'\n'}) {
                i += 1;
                continue;
            }
            col += static_cast<std::uint64_t>(display_width(d, col, tab_width_));
            i += std::max<std::size_t>(1, d.len);
        }
        off += i;
    }
    return col;
}

int EditorView::gutter_width() const {
    if (!line_numbers_) return 0;
    const PieceTree& t = doc_.text();
    // Known count, or an estimate from the size, so the text does not jump as counts arrive.
    const std::uint64_t lines = t.line_count().value_or(t.size() / 16 + 1);
    return std::max(3, digits(lines)) + 1;
}

void EditorView::scroll_to_cursor(int area_rows, int area_cols) { scroll_to(editor_.cursor(), area_rows, area_cols, kRowMargin, false); }

void EditorView::scroll_to(std::uint64_t pos, int area_rows, int area_cols, int row_margin, bool margin_above) {
    if (const ReadingLayout* r = layout_for(area_cols)) {
        const std::size_t rows = static_cast<std::size_t>(std::max(1, area_rows));
        reading_rows_ = rows;
        const ReadingLayout::Spot at = r->locate(std::min(pos, doc_.text().size()));
        if (at.line < rtop_) rtop_ = at.line;
        if (at.line >= rtop_ + rows) rtop_ = at.line + 1 - rows;
        // Sideways, for a wide table or code line: the cursor's column stays on screen with a
        // margin, and the view comes back to column 0 whenever the column fits from there.
        const int gutter = std::min(gutter_width(), std::max(0, area_cols - 1));
        const int text_cols = std::max(1, area_cols - gutter);
        const int cmargin = std::min(kColMargin, (text_cols - 1) / 2);
        if (at.col + cmargin < text_cols) {
            rhscroll_ = 0;
        } else if (at.col < rhscroll_ + cmargin) {
            rhscroll_ = std::max(0, at.col - cmargin);
        } else if (at.col + cmargin >= rhscroll_ + text_cols) {
            rhscroll_ = at.col + cmargin + 1 - text_cols;
        }
        return;
    }
    if (wrap_) {
        scroll_wrapped(pos, area_rows, area_cols, row_margin, margin_above);
        return;
    }
    const PieceTree& t = doc_.text();
    const std::uint64_t cursor = std::min(pos, t.size());
    const std::uint64_t cur_line = line_start_of(cursor);
    const int rows = std::max(1, area_rows);
    const int margin = std::min(row_margin, (rows - 1) / 2);

    top_ = line_start_of(top_);
    // With `margin_above`, a line in the top margin rows also moves the view, down to the margin.
    int above = 0;
    if (margin_above && cur_line >= top_) {
        for (std::uint64_t l = top_; l != npos && l < cur_line && above <= margin; l = next_line(l)) ++above;
    }
    if (cur_line < top_ || (margin_above && above < margin)) {
        top_ = cur_line;
        for (int i = 0; i < margin && top_ > 0; ++i) top_ = line_start_of(top_ - 1);
    } else {
        // Is the cursor line within the first rows - margin lines from top?
        std::uint64_t l = top_;
        int row = 0;
        while (l != npos && l < cur_line && row < rows) {
            l = next_line(l);
            ++row;
        }
        if (l != cur_line || row > rows - 1 - margin) {
            std::uint64_t s = cur_line;
            for (int i = 0; i < rows - 1 - margin && s > 0; ++i) s = line_start_of(s - 1);
            top_ = s;
        }
    }

    const int text_cols = std::max(1, area_cols - gutter_width());
    const int cmargin = std::min(kColMargin, (text_cols - 1) / 2);
    const std::uint64_t col = column_of(cur_line, cursor);
    if (col < hscroll_ + static_cast<std::uint64_t>(cmargin)) {
        hscroll_ = col > static_cast<std::uint64_t>(cmargin) ? col - static_cast<std::uint64_t>(cmargin) : 0;
    } else if (col + static_cast<std::uint64_t>(cmargin) >= hscroll_ + static_cast<std::uint64_t>(text_cols)) {
        hscroll_ = col + static_cast<std::uint64_t>(cmargin) + 1 - static_cast<std::uint64_t>(text_cols);
    }
    if (highlighter_) {
        std::uint64_t last = top_;
        for (int i = 0; i < rows && last != npos; ++i) last = next_line(last);
        highlighter_->visible_range_changed(top_, last == npos ? t.size() : last);
    }
}

// As scroll_to_cursor, in screen rows: `top` is the start of a row, which may lie inside a line.
void EditorView::scroll_wrapped(std::uint64_t pos, int area_rows, int area_cols, int row_margin, bool margin_above) {
    const PieceTree& t = doc_.text();
    const WrapLayout wrap(t, wrap_cols(area_cols), tab_width_);
    const std::uint64_t cursor = std::min(pos, t.size());
    const std::uint64_t cur_row = wrap.row_start(cursor);
    const int rows = std::max(1, area_rows);
    const int margin = std::min(row_margin, (rows - 1) / 2);
    auto back = [&](std::uint64_t row, int n) {
        for (int i = 0; i < n; ++i) {
            const std::uint64_t prev = wrap.prev_row(row);
            if (prev == npos) break;
            row = prev;
        }
        return row;
    };

    hscroll_ = 0;
    top_ = wrap.row_start(std::min(top_, t.size()));  // a resize or an edit may have moved the rows
    int above = 0;
    if (margin_above && cur_row >= top_) {
        for (std::uint64_t r = top_; r != npos && r < cur_row && above <= margin; r = wrap.next_row(r)) ++above;
    }
    if (cur_row < top_ || (margin_above && above < margin)) {
        top_ = back(cur_row, margin);
    } else {
        // Is the cursor row within the first rows - margin rows from top?
        std::uint64_t r = top_;
        int n = 0;
        while (r != npos && r < cur_row && n < rows) {
            r = wrap.next_row(r);
            ++n;
        }
        if (r != cur_row || n > rows - 1 - margin) top_ = back(cur_row, rows - 1 - margin);
    }
    if (highlighter_) {
        std::uint64_t last = top_;
        for (int i = 0; i < rows && last != npos; ++i) last = wrap.next_row(last);
        highlighter_->visible_range_changed(wrap.line_start(top_), last == npos ? t.size() : last);
    }
}

void EditorView::load_spans(std::uint64_t line, std::uint64_t end) {
    spans_.clear();
    if (preview_) {
        for (const PreviewMark& m : *preview_) {
            if (m.end <= line || m.start >= end) continue;
            spans_.push_back(StyleSpan{std::max(m.start, line), std::min(m.end, end), m.removed ? Style::history_removed : Style::history_inserted, 0});
        }
        return;
    }
    if (!highlighter_) return;
    line_buf_ = doc_.text().read(line, std::min(end - line, kReadSlice));
    try {
        spans_ = highlighter_->spans_for_line(line, line_buf_);
    } catch (...) {
        spans_.clear();  // a highlighter never breaks rendering
    }
}

EditorView::Drawn EditorView::draw_text(Screen& screen, int row, int text_col, int right, std::uint64_t from, std::uint64_t to,
                                        std::uint64_t hscroll, const std::optional<Match>& search) {
    const PieceTree& t = doc_.text();
    const Attr plain = attr_for(Style::Default);
    const Attr dim = attr_for(Style::md_markup);
    const auto sel = preview_ ? std::nullopt : editor_.selection();  // a preview has no selection
    const auto text_cols = static_cast<std::uint64_t>(std::max(0, right - text_col));

    // Far along a long line, start from the nearest measured point before the window.
    std::uint64_t col = 0;
    if (hscroll > 0) std::tie(from, col) = column_start(from, hscroll);
    // Bytes of the row, read in slices until the right edge is reached.
    row_buf_ = t.read(from, std::min(to - from, kReadSlice));
    std::size_t span_i = 0;
    std::size_t i = 0;
    std::uint64_t base = from;  // offset of row_buf_[0]
    const std::uint64_t limit_col = hscroll + text_cols;
    int last_drawn = text_col - 1;  // screen column of the last base cell, for zero-width joins
    while (col < limit_col) {
        if (i + 4 > row_buf_.size() && base + row_buf_.size() < to) {
            // Refill, keeping the undecoded tail.
            row_buf_.erase(0, i);
            base += i;
            i = 0;
            const std::uint64_t have = base + row_buf_.size();
            row_buf_ += t.read(have, std::min(kReadSlice, to - have));
        }
        if (i >= row_buf_.size()) break;
        const std::uint64_t off = base + i;
        const Decoded d = decode(std::as_bytes(std::span(row_buf_.data() + i, row_buf_.size() - i)));
        const std::size_t len = std::max<std::size_t>(1, d.len);
        const int w = display_width(d, col, tab_width_);

        Attr a = plain;
        while (span_i < spans_.size() && spans_[span_i].end <= off) ++span_i;
        if (span_i < spans_.size() && spans_[span_i].start <= off) a = attr_for(spans_[span_i].style, spans_[span_i].modifiers);
        std::string glyph;
        bool escape = false;
        if (!d.valid) {
            glyph = std::format("\\x{:02X}", static_cast<unsigned>(d.cp & 0xFF));
            escape = true;
        } else if (d.cp == '\t') {
            glyph.assign(static_cast<std::size_t>(std::max(0, w)), ' ');
            escape = true;  // drawn cell by cell, but in the text's style
        } else if (d.cp < 0x20 || d.cp == 0x7F) {
            glyph = std::string("^") + static_cast<char>(d.cp == 0x7F ? '?' : d.cp + 0x40);
            escape = true;
        } else if (d.cp >= 0x80 && d.cp <= 0x9F) {
            glyph = std::format("\\u{:04X}", static_cast<unsigned>(d.cp));
            escape = true;
        }
        if (escape && d.cp != '\t') a = dim;
        if (search && off >= search->start && off < search->end) a = overlay(a, Style::search_match);
        if (sel && off >= sel->first && off < sel->second) a = overlay(a, Style::selection);
        a = on_page(a);  // last, so the page's reverse inverts the selection too

        if (w == 0) {
            if (col >= hscroll && last_drawn >= text_col) screen.put(row, last_drawn + 1, row_buf_.substr(i, len), 0, a);
        } else if (escape) {
            for (int c = 0; c < w && c < static_cast<int>(glyph.size()); ++c) {
                const std::uint64_t cc = col + static_cast<std::uint64_t>(c);
                if (cc < hscroll || cc >= limit_col) continue;
                const int sc = text_col + static_cast<int>(cc - hscroll);
                screen.put(row, sc, std::string_view(glyph).substr(static_cast<std::size_t>(c), 1), 1, a);
                last_drawn = sc;
            }
        } else if (col >= hscroll) {
            const int sc = text_col + static_cast<int>(col - hscroll);
            if (col + static_cast<std::uint64_t>(w) > limit_col) {
                screen.put(row, sc, " ", 1, a);  // a wide character cut by the right edge
            } else {
                screen.put(row, sc, row_buf_.substr(i, len), w, a);
            }
            last_drawn = sc;
        } else if (col + static_cast<std::uint64_t>(w) > hscroll) {
            screen.put(row, text_col, " ", 1, a);  // cut by the left edge
            last_drawn = text_col;
        }
        col += static_cast<std::uint64_t>(std::max(0, w));
        i += len;
    }
    // Text is hidden on the right when bytes remain or the last unit was cut by the edge.
    const bool more = i < row_buf_.size() || base + row_buf_.size() < to;
    return Drawn{col, more || col > limit_col};
}

void EditorView::render(Screen& screen, Rect area, const std::optional<Match>& search, bool focused) {
    if (layout_for(area.cols)) {
        render_reading(screen, area, search, focused);
        return;
    }
    if (wrap_) {
        render_wrapped(screen, area, search, focused);
        return;
    }
    const PieceTree& t = doc_.text();
    const std::uint64_t size = t.size();
    const int gutter = std::min(gutter_width(), std::max(0, area.cols - 1));
    const int text_col = area.col + gutter;
    const int right = area.col + area.cols;
    const auto text_cols = static_cast<std::uint64_t>(std::max(0, right - text_col));
    const Attr plain = on_page(attr_for(Style::Default));
    const auto sel = preview_ ? std::nullopt : editor_.selection();  // a preview has no selection
    const std::uint64_t cursor = std::min(editor_.cursor(), size);

    top_ = std::min(top_, size);
    const std::optional<std::uint64_t> top_line = line_numbers_ ? doc_.line_of(top_, false) : std::nullopt;
    const std::uint64_t cursor_line_start = line_start_of(cursor);
    const std::uint64_t limit_col = hscroll_ + text_cols;

    std::uint64_t line = top_;
    bool past_end = false;
    for (int r = 0; r < area.rows; ++r) {
        const int row = area.row + r;
        screen.fill(row, area.col, right, plain);
        if (past_end) {
            draw_past_end(screen, row, area.col, gutter);
            continue;
        }
        const std::uint64_t lf = line < size ? t.find_lf_forward(line, size - line) : npos;
        std::uint64_t end = lf == npos ? size : lf;
        if (lf != npos && lf > line && t.byte_at(lf - 1) == std::byte{'\r'}) end = lf - 1;  // CR before LF is hidden

        if (gutter > 0) {
            const bool is_current = line == cursor_line_start;
            std::string label = top_line ? std::to_string(*top_line + 1 + static_cast<std::uint64_t>(r)) : std::string("…");
            const int w = gutter - 1;
            const int pad = std::max(0, w - static_cast<int>(top_line ? label.size() : 1));
            screen.print(row, area.col + pad, text_col, label, on_page(attr_for(is_current ? Style::gutter_current : Style::gutter)));
        }

        load_spans(line, end);
        const Drawn drawn = draw_text(screen, row, text_col, right, line, end, hscroll_, search);
        // The selection covers the line feed: show one selected cell after the text.
        if (sel && lf != npos && lf >= sel->first && lf < sel->second && drawn.col >= hscroll_ && drawn.col < limit_col)
            screen.put(row, text_col + static_cast<int>(drawn.col - hscroll_), " ", 1, on_page(attr_for(Style::selection)));
        // The line runs past the right edge: mark it in the last column.
        if (drawn.overflow && right > text_col) screen.put(row, right - 1, ">", 1, overflow_attr());

        if (lf == npos) {
            past_end = true;
        } else {
            line = lf + 1;
        }
    }

    if (focused) {
        // The cursor row is found by walking from top; it is on screen after scroll_to_cursor.
        std::uint64_t l = top_;
        int r = 0;
        while (r < area.rows && l != cursor_line_start && l != npos && l <= cursor_line_start) {
            l = next_line(l);
            ++r;
        }
        if (l == cursor_line_start && r < area.rows) {
            const std::uint64_t col = column_of(cursor_line_start, cursor);
            if (col >= hscroll_ && col - hscroll_ < text_cols) {
                screen.set_cursor(area.row + r, text_col + static_cast<int>(col - hscroll_), true);
                return;
            }
        }
        screen.set_cursor(0, 0, false);
    }
}

// Reading: the rendered lines from `rtop_`, each byte in its span's style (none without a
// highlighter), with the selection and the search match over the bytes whose source is in them.
void EditorView::render_reading(Screen& screen, Rect area, const std::optional<Match>& search, bool focused) {
    const ReadingLayout& r = *layout_;
    const auto& lines = r.page().lines;
    const std::uint64_t size = doc_.text().size();
    const int gutter = std::min(gutter_width(), std::max(0, area.cols - 1));
    const int text_col = area.col + gutter;
    const int right = area.col + area.cols;
    const Attr plain = on_page(attr_for(Style::Default));
    const auto sel = editor_.selection();
    const std::uint64_t cursor = std::min(editor_.cursor(), size);
    const ReadingLayout::Spot at = r.locate(cursor);
    const std::uint32_t cursor_source_line = at.line < lines.size() ? lines[at.line].source_line : 0;
    reading_rows_ = static_cast<std::size_t>(std::max(1, area.rows));
    rtop_ = std::min(rtop_, lines.empty() ? 0 : lines.size() - 1);
    for (int n = 0; n < area.rows; ++n) {
        const int row = area.row + n;
        screen.fill(row, area.col, right, plain);
        const std::size_t i = rtop_ + static_cast<std::size_t>(n);
        if (i >= lines.size()) {
            draw_past_end(screen, row, area.col, gutter);
            continue;
        }
        const RenderedLine& line = lines[i];
        // A source line's number on the first rendered line that comes from it.
        if (gutter > 0 && (i == 0 || lines[i - 1].source_line != line.source_line)) {
            const std::string label = std::to_string(line.source_line);
            const int pad = std::max(0, gutter - 1 - static_cast<int>(label.size()));
            const bool current = line.source_line == cursor_source_line;
            screen.print(row, area.col + pad, text_col, label, on_page(attr_for(current ? Style::gutter_current : Style::gutter)));
        }
        // Panned by `rhscroll_` columns; a line running past the right edge gives its last
        // column to the overflow marker.
        int width = 0;
        for (std::size_t b = 0; b < line.text.size();) {
            const Decoded d = decode(std::as_bytes(std::span(line.text.data() + b, line.text.size() - b)));
            width += d.valid ? std::max(0, display_width(d, 0, 1)) : 1;
            b += std::max<std::size_t>(1, d.len);
        }
        const bool overflow = width - rhscroll_ > right - text_col;
        const int end = overflow ? right - 1 : right;
        int col = text_col;
        int line_col = 0;  // display column within the line
        std::size_t span = 0;
        for (std::size_t b = 0; b < line.text.size() && col < end;) {
            const Decoded d = decode(std::as_bytes(std::span(line.text.data() + b, line.text.size() - b)));
            const std::size_t len = std::max<std::size_t>(1, d.len);
            const int w = d.valid ? std::max(0, display_width(d, 0, 1)) : 1;
            const int at_col = line_col;
            line_col += w;
            if (at_col < rhscroll_) {
                // Before the panned edge; a wide character cut by it leaves a blank cell.
                if (line_col > rhscroll_) col += line_col - rhscroll_;
                b += len;
                continue;
            }
            Attr a = attr_for(Style::Default);
            while (span < line.spans.size() && line.spans[span].end <= b) ++span;
            if (highlighter_ && span < line.spans.size() && line.spans[span].begin <= b) a = attr_for(line.spans[span].style);
            const std::uint64_t src = b < line.source.size() ? line.source[b] : kGenerated;
            if (src != kGenerated) {
                if (search && src >= search->start && src < search->end) a = overlay(a, Style::search_match);
                if (sel && src >= sel->first && src < sel->second) a = overlay(a, Style::selection);
            }
            col = screen.print(row, col, end, std::string_view(line.text).substr(b, len), on_page(a));
            b += len;
        }
        if (overflow && right > text_col) screen.put(row, right - 1, ">", 1, overflow_attr());
    }
    if (!focused) return;
    const int cursor_col = text_col + at.col - rhscroll_;
    if (at.line >= rtop_ && at.line < rtop_ + static_cast<std::size_t>(area.rows) && cursor_col >= text_col && cursor_col < right) {
        screen.set_cursor(area.row + static_cast<int>(at.line - rtop_), cursor_col, true);
    } else {
        screen.set_cursor(0, 0, false);
    }
}

// Soft wrap: one screen row per wrapped row, no sideways scrolling and no overflow marker.
void EditorView::render_wrapped(Screen& screen, Rect area, const std::optional<Match>& search, bool focused) {
    const PieceTree& t = doc_.text();
    const std::uint64_t size = t.size();
    const int gutter = std::min(gutter_width(), std::max(0, area.cols - 1));
    const int text_col = area.col + gutter;
    const int right = area.col + area.cols;
    const int text_cols = std::max(0, right - text_col);
    const Attr plain = on_page(attr_for(Style::Default));
    const auto sel = preview_ ? std::nullopt : editor_.selection();  // a preview has no selection
    const std::uint64_t cursor = std::min(editor_.cursor(), size);
    const WrapLayout wrap(t, wrap_cols(area.cols), tab_width_);

    top_ = wrap.row_start(std::min(top_, size));
    std::uint64_t line = wrap.line_start(top_);
    std::uint64_t line_end = wrap.line_end(line);
    const std::optional<std::uint64_t> top_line = line_numbers_ ? doc_.line_of(line, false) : std::nullopt;
    const std::uint64_t cursor_line_start = wrap.line_start(cursor);
    const std::uint64_t cursor_row = wrap.row_start(cursor);
    std::uint64_t lines_down = 0;  // lines between the top line and the one being drawn
    bool spans_loaded = false;
    bool cursor_set = false;

    std::uint64_t r = top_;
    bool past_end = false;
    for (int n = 0; n < area.rows; ++n) {
        const int row = area.row + n;
        screen.fill(row, area.col, right, plain);
        if (past_end) {
            draw_past_end(screen, row, area.col, gutter);
            continue;
        }
        const std::uint64_t end = wrap.row_end(r);
        const bool last_row = end >= line_end;

        // Only a line's first row carries its number.
        if (gutter > 0 && r == line) {
            const bool is_current = line == cursor_line_start;
            std::string label = top_line ? std::to_string(*top_line + 1 + lines_down) : std::string("…");
            const int w = gutter - 1;
            const int pad = std::max(0, w - static_cast<int>(top_line ? label.size() : 1));
            screen.print(row, area.col + pad, text_col, label, on_page(attr_for(is_current ? Style::gutter_current : Style::gutter)));
        }
        if (!spans_loaded) {
            load_spans(line, line_end);
            spans_loaded = true;
        }
        const Drawn drawn = draw_text(screen, row, text_col, right, r, end, 0, search);
        const std::uint64_t lf = line_end < size ? t.find_lf_forward(line_end, size - line_end) : npos;
        // The selection covers the line feed: show one selected cell after the text.
        if (last_row && sel && lf != npos && lf >= sel->first && lf < sel->second && drawn.col < static_cast<std::uint64_t>(text_cols))
            screen.put(row, text_col + static_cast<int>(drawn.col), " ", 1, on_page(attr_for(Style::selection)));
        if (focused && r == cursor_row && text_cols > 0) {
            // Whitespace hanging past the wrap width keeps the cursor in the last column.
            const std::uint64_t col = std::min<std::uint64_t>(column_of(r, cursor), static_cast<std::uint64_t>(text_cols - 1));
            screen.set_cursor(row, text_col + static_cast<int>(col), true);
            cursor_set = true;
        }

        if (!last_row) {
            r = end;
        } else if (lf == npos) {
            past_end = true;
        } else {
            r = line = lf + 1;
            line_end = wrap.line_end(line);
            ++lines_down;
            spans_loaded = false;
        }
    }
    if (focused && !cursor_set) screen.set_cursor(0, 0, false);
}

void draw_status_line(Screen& screen, int row, std::string_view left, std::string_view message, std::string_view right, bool unfocused, int col,
                      int width) {
    const Attr bar = unfocused ? active_theme().unfocused_status() : attr_for(Style::status);
    const int x0 = std::clamp(col, 0, screen.cols());
    const int cols = width < 0 ? screen.cols() : std::min(screen.cols(), x0 + width);  // the right edge
    screen.fill(row, x0, cols, bar);
    const int lw = screen.print(row, x0, cols, left, bar) - x0;
    const int rw = text_columns(right);
    const int mw = text_columns(message);
    constexpr int kGap = 2;  // between the message and either block
    if (message.empty()) {
        if (x0 + lw + 1 + rw <= cols) screen.print(row, cols - rw, cols, right, bar);
        return;
    }
    // The message is centered on the line, or as near the center as the blocks allow.
    const int lo = x0 + lw + kGap;
    const int hi = cols - rw - kGap;  // the message must end by here to keep the right block
    if (lo + mw <= hi) {
        const int start = std::clamp(x0 + (cols - x0 - mw) / 2, lo, hi - mw);
        screen.print(row, start, cols, message, bar);
        screen.print(row, cols - rw, cols, right, bar);
    } else {
        // Too long to share the row: the right block gives way.
        screen.print(row, lo, cols, message, bar);
    }
}

// The '>' ending a cut line looks like its split's status line: focused, or not.
Attr EditorView::overflow_attr() const { return split_focused_ ? attr_for(Style::overflow_marker) : active_theme().unfocused_status(); }

void EditorView::render_status(Screen& screen, int row, std::string_view message, StatusMark mark, int col, int width) {
    const std::uint64_t cursor = std::min(editor_.cursor(), doc_.text().size());
    const std::uint64_t ls = line_start_of(cursor);
    const auto line = doc_.line_of(cursor, false);
    std::string left = mark == StatusMark::focused ? ">" : " ";
    left += doc_.is_untitled() ? std::string("[untitled]") : doc_.path().filename().string();
    if (doc_.is_dirty()) left += " *";
    if (doc_.read_only_file()) left += " [read-only]";
    if (read_only_) left += " [view]";
    std::string right = line ? std::format("{}:{}", *line + 1, column_of(ls, cursor) + 1) : std::format("…:{}", column_of(ls, cursor) + 1);
    if (const auto count = doc_.text().line_count()) right += std::format(" / {}", *count);
    // A read-only view never edits, so its history state is not news.
    if (const std::string_view h = read_only_ ? std::string_view() : history_label(doc_.history_state()); !h.empty()) right += std::format("  {}", h);
    if (const auto current = doc_.history().current()) {
        const NodeId parent = doc_.history().meta(*current).parent;
        if (parent != kNoParent && doc_.history().contains(parent)) {
            const NodeInfo info = doc_.history().node_info(parent);
            if (info.children.size() > 1) {
                const auto it = std::find(info.children.begin(), info.children.end(), *current);
                right += std::format("  branch {}/{}", (it - info.children.begin()) + 1, info.children.size());
            }
        }
    }
    if (highlighter_) {
        if (const std::string s = highlighter_->status(); !s.empty()) right += "  " + s;
    }
    right += " ";
    draw_status_line(screen, row, left, message, right, mark == StatusMark::unfocused, col, width);
}

void EditorView::after_change(const ChangeEvent& ev) {
    if (ev.offset < top_) {
        if (ev.offset + ev.removed_len <= top_) {
            top_ = top_ - ev.removed_len + ev.inserted_len;
        } else {
            top_ = ev.offset;
        }
    }
    // With wrap on, `top` is a row inside a line; the next scroll or frame snaps it to a row.
    if (!wrap_) top_ = line_start_of(top_);
}

void EditorView::reloaded() {
    top_ = 0;
    hscroll_ = 0;
}

}  // namespace mod
