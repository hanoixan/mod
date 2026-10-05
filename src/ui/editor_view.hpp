#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "search/regex.hpp"
#include "syntax/highlight.hpp"
#include "ui/reading_layout.hpp"
#include "ui/screen.hpp"

namespace mod {

// The text area (with its gutter) and the status line. Holds its position as the byte
// offset of the first visible line. Main thread.
// The status line's three blocks: `left`, `message` centered between the blocks, and
// `right` at the edge; the right block gives way to a message too long to share the row.
// How a status line shows which view has the focus: the focused one starts with '>' (with
// one view too), the others are drawn on a darker band; `none`, neither.
enum class StatusMark { none, focused, unfocused };

void draw_status_line(Screen& screen, int row, std::string_view left, std::string_view message, std::string_view right, bool unfocused = false,
                      int col = 0, int width = -1);  // from `col`, `width` columns (-1: to the screen's edge)

class EditorView final : public DocumentListener {
public:
    static constexpr int kRowMargin = 3;
    static constexpr int kColMargin = 8;

    EditorView(Document& doc, const Editor& editor, Highlighter* highlighter, int tab_width = 4);
    ~EditorView() override;
    EditorView(const EditorView&) = delete;
    EditorView& operator=(const EditorView&) = delete;

    void render(Screen& screen, Rect area, const std::optional<Match>& search_highlight, bool focused);
    // Whether this view's split has the focus (true unless App says otherwise): a cut line's
    // '>' then takes the focused or the unfocused status look.
    void set_split_focused(bool on) noexcept { split_focused_ = on; }
    void render_status(Screen& screen, int row, std::string_view message, StatusMark mark = StatusMark::none, int col = 0, int width = -1);
    void scroll_to_cursor(int area_rows, int area_cols);
    // Scrolls the least that puts `pos` on screen, at least `row_margin` rows above the bottom
    // (fewer on a short view), and, with `margin_above`, as many below the top too; without it,
    // a place above the view comes in `row_margin` rows down. In a document laid out for
    // reading, its line on screen. scroll_to_cursor is this for the cursor: kRowMargin, not above.
    void scroll_to(std::uint64_t pos, int area_rows, int area_cols, int row_margin, bool margin_above);
    void set_tab_width(int width);
    void set_line_numbers(bool on) { line_numbers_ = on; }
    bool line_numbers() const noexcept { return line_numbers_; }
    void set_highlighter(Highlighter* highlighter) { highlighter_ = highlighter; }
    // Preview mode, for the Undo History pane: the (previewed) text is drawn read-only with
    // these marks instead of the highlighter's spans, and no selection; nullopt ends it.
    void set_preview(std::optional<std::vector<PreviewMark>> marks) { preview_ = std::move(marks); }
    bool previewing() const noexcept { return preview_.has_value(); }
    // Soft wrap: long lines continue on the next rows instead of scrolling sideways.
    void set_word_wrap(bool on);
    bool word_wrap() const noexcept { return wrap_; }
    // Shows the line (or, with word wrap, the row) containing `offset` first.
    void set_top(std::uint64_t offset);
    // Marks the status line "[view]" while the document is in read-only mode.
    void set_read_only(bool on) { read_only_ = on; }
    // Draws the document laid out for reading (Markdown in read-only mode) while the text
    // area is wide enough; otherwise, and when off, as lines.
    void set_reading(bool on);
    bool reading() const noexcept { return reading_; }
    // The layout the last frame drew, or nullptr when it drew lines.
    const ReadingLayout* reading_layout() const noexcept { return layout_.get(); }
    // The column Up and Down aim for while reading; App keeps it here, one per view.
    std::optional<int>& reading_sticky() noexcept { return reading_sticky_; }
    // Rendered lines per screen while reading, from the last frame.
    std::size_t reading_rows() const noexcept { return reading_rows_; }
    // The columns a wrapped row may fill in a text area `area_cols` wide (gutter included):
    // one less than the text columns, so the cursor at a row's end has a cell.
    int wrap_cols(int area_cols) const;

    std::uint64_t top() const noexcept { return top_; }
    std::uint64_t hscroll() const noexcept { return hscroll_; }

    void after_change(const ChangeEvent& ev) override;
    void reloaded() override;

private:
    Attr overflow_attr() const;
    bool split_focused_ = true;
    std::uint64_t line_start_of(std::uint64_t pos) const;
    // The start of the line after the one starting at `line_start`, or npos at the last line.
    std::uint64_t next_line(std::uint64_t line_start) const;
    std::uint64_t column_of(std::uint64_t line_start, std::uint64_t pos) const;
    // Checkpoints along the line last measured, for the text as it was then: every
    // kColumnMarkEvery bytes, the (offset, column) of a character start, ascending, the
    // first being the line start at column 0. Measuring and drawing far along a very long
    // line start from the nearest one instead of the line start.
    struct ColumnIndex {
        bool valid = false;
        std::uint64_t version = 0;
        std::uint64_t size = 0;
        int tab_width = 0;
        std::uint64_t line_start = 0;
        std::vector<std::pair<std::uint64_t, std::uint64_t>> marks;
        std::uint64_t measured_to = 0;  // the line has no line feed before this offset
        // Whether these checkpoints are for `line` of `doc`'s text as it is now.
        bool holds(const Document& doc, int tab, std::uint64_t line) const;
    };
    static constexpr std::uint64_t kColumnMarkEvery = 4 * 1024;
    mutable ColumnIndex columns_;
    // The index for the line at `line_start`, begun afresh when it holds another.
    ColumnIndex& column_index(std::uint64_t line_start) const;
    // The checkpoint nearest before display column `col` of the line at `line_start`, as
    // far as that line has been measured: (offset, column).
    std::pair<std::uint64_t, std::uint64_t> column_start(std::uint64_t line_start, std::uint64_t col) const;
    int gutter_width() const;
    struct Drawn {
        std::uint64_t col = 0;   // the display column after the last unit drawn or skipped
        bool overflow = false;   // text continues past the right edge
    };
    // Draws the bytes [from, to) of one line on `row`, the byte at `from` in display column 0;
    // columns before `hscroll` are skipped.
    Drawn draw_text(Screen& screen, int row, int text_col, int right, std::uint64_t from, std::uint64_t to,
                    std::uint64_t hscroll, const std::optional<Match>& search);
    // Fills `spans_` for the line [line, end).
    void load_spans(std::uint64_t line, std::uint64_t end);
    void render_wrapped(Screen& screen, Rect area, const std::optional<Match>& search, bool focused);
    // The layout for a text area `area_cols` wide (gutter included), made again when the
    // text or the width changed; nullptr when reading is off or the area is too narrow.
    const ReadingLayout* layout_for(int area_cols);
    void render_reading(Screen& screen, Rect area, const std::optional<Match>& search, bool focused);
    void scroll_wrapped(std::uint64_t pos, int area_rows, int area_cols, int row_margin, bool margin_above);

    Document& doc_;
    const Editor& editor_;
    Highlighter* highlighter_;
    int tab_width_;
    bool line_numbers_ = true;
    bool wrap_ = false;
    bool read_only_ = false;
    std::uint64_t top_ = 0;
    std::uint64_t hscroll_ = 0;
    std::string line_buf_;            // per-frame scratch, reused
    std::string row_buf_;             // per-frame scratch, reused
    std::vector<StyleSpan> spans_;    // per-frame scratch, reused
    std::optional<std::vector<PreviewMark>> preview_;
    bool reading_ = false;
    std::unique_ptr<ReadingLayout> layout_;
    std::uint64_t layout_version_ = 0;
    int layout_width_ = 0;
    std::size_t rtop_ = 0;  // the first rendered line shown
    int rhscroll_ = 0;      // the first display column shown, panned for wide lines
    std::size_t reading_rows_ = 1;
    std::optional<int> reading_sticky_;
};

}  // namespace mod
