#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "edit/clipboard.hpp"
#include "edit/document.hpp"
#include "text/utf8.hpp"
#include "util/error.hpp"

namespace mod {

enum class Motion { Left, Right, WordLeft, WordRight, Up, Down, LineStart, LineEnd, PageUp, PageDown, DocStart, DocEnd };

// Cursor, selection and editing commands over a Document. Main thread.
class Editor : public DocumentListener {
public:
    Editor(Document& doc, Clipboard& clipboard, int tab_width = 4);
    ~Editor() override;
    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    void move(Motion motion, bool extend = false, std::uint64_t page_rows = 1);
    void insert_text(std::string_view bytes, EditKind kind = EditKind::typing);
    void set_tab_width(int width);
    // Tab: spaces up to the next tab stop, or a tab character; replaces a selection.
    void indent(bool spaces);
    // Shift+Tab: removes up to a tab width of leading spaces, or one leading tab, from
    // every line the selection touches (the cursor's line without one), as one undo step.
    void outdent();
    // With a width, Up, Down and the page keys move by soft-wrapped screen row of that
    // many columns; with none, by line.
    void set_wrap_width(std::optional<int> columns);
    void newline();
    void delete_backward(bool word = false);
    void delete_forward(bool word = false);
    bool copy();
    void cut();
    void paste();
    // Ctrl+K: cuts the selection, or from the cursor to the line end, or the line break at
    // the line end. `append` joins the cut onto the clipboard instead of replacing it.
    void cut_to_line_end(bool append);
    // Text pasted from the terminal: every line break becomes the document's line ending.
    // `more` says another piece of the same paste follows; the pieces are one undo step.
    void paste_text(std::string_view bytes, bool more = false);
    void select_range(std::uint64_t start, std::uint64_t end);
    Status undo();
    Status redo();

    std::uint64_t cursor() const noexcept { return cursor_; }
    std::optional<std::uint64_t> anchor() const noexcept { return anchor_; }
    std::optional<std::pair<std::uint64_t, std::uint64_t>> selection() const;
    int tab_width() const noexcept { return tab_width_; }

    void after_change(const ChangeEvent& ev) override;
    void reloaded() override;

private:
    void insert_pasted(std::string_view bytes);
    std::uint64_t size() const;
    std::uint64_t snap(std::uint64_t pos) const;
    std::uint64_t next_boundary(std::uint64_t pos) const;
    std::uint64_t prev_boundary(std::uint64_t pos) const;
    CharClass class_at(std::uint64_t pos) const;
    std::uint64_t word_right(std::uint64_t pos) const;
    std::uint64_t word_left(std::uint64_t pos) const;
    std::uint64_t line_start(std::uint64_t pos) const;
    std::uint64_t line_end(std::uint64_t pos) const;
    std::uint64_t column_at(std::uint64_t line_start, std::uint64_t pos);
    std::uint64_t offset_at_column(std::uint64_t line_start, std::uint64_t column);
    std::uint64_t vertical(std::uint64_t pos, std::int64_t lines);
    std::uint64_t vertical_rows(std::uint64_t pos, std::int64_t rows, int width);
    void set_cursor(std::uint64_t pos, bool extend);
    void delete_selection(EditKind kind);

    Document& doc_;
    bool pasting_ = false;  // inside a paste whose last piece has not come
    Clipboard& clipboard_;
    int tab_width_;
    std::optional<int> wrap_width_;
    std::uint64_t cursor_ = 0;
    std::optional<std::uint64_t> anchor_;
    std::optional<std::uint64_t> sticky_column_;
    // (line start) -> (offset, column) every 4 KiB along long lines; cleared on change.
    std::map<std::uint64_t, std::vector<std::pair<std::uint64_t, std::uint64_t>>> checkpoints_;
};

}  // namespace mod
