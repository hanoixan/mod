#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

#include "edit/clipboard.hpp"
#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "edit/sidecar.hpp"
#include "platform/terminal.hpp"
#include "text/wrap.hpp"
#include "ui/editor_view.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"
#include "util/event_queue.hpp"
#include "time_budget.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

class NullTerminal : public Terminal {
public:
    Status enter_raw_mode() override { return {}; }
    void restore() noexcept override {}
    TerminalSize size() override { return {}; }
    Result<std::size_t> read_input(std::span<std::byte>) override { return 0; }
    Status write(std::span<const std::byte>) override { return {}; }
    WaitEvents wait(int) override { return timed_out; }
    void wake() noexcept override {}
};

// A document holding `text`, an editor, a view, and a screen `rows` x `cols` whose
// whole area is the text area.
struct Fixture {
    EventQueue q{{}};
    Clipboard clipboard;
    std::unique_ptr<Document> doc;
    std::unique_ptr<Editor> ed;
    std::unique_ptr<EditorView> view;
    NullTerminal term;
    Screen screen{term};
    Rect area;

    Fixture(const std::string& name, std::string_view text, int rows, int cols, bool line_numbers = false, bool history = true)
        : area{0, 0, rows, cols} {
        const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "editor_view_test";
        fs::create_directories(dir);
        const fs::path p = dir / name;
        fs::remove(p);
        fs::remove(sidecar_path_for(p));
        {
            std::ofstream out(p, std::ios::binary | std::ios::trunc);
            out.write(text.data(), static_cast<std::streamsize>(text.size()));
        }
        DocumentOptions options;
        options.history = history;
        auto d = Document::open(p, q, options);
        REQUIRE(d);
        doc = std::move(*d);
        // Line numbers come from the background scan; run its result before drawing.
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
        while (!doc->text().line_count()) {
            if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            REQUIRE(std::chrono::steady_clock::now() < deadline);
        }
        ed = std::make_unique<Editor>(*doc, clipboard, 4);
        view = std::make_unique<EditorView>(*doc, *ed, nullptr, 4);
        view->set_line_numbers(line_numbers);
        screen.resize({rows, cols});
    }
    ~Fixture() {
        view.reset();
        ed.reset();
    }

    // What App does around every command: keep the editor's wrap width in step, scroll, draw.
    void draw(bool focused = true) {
        ed->set_wrap_width(view->word_wrap() ? std::optional<int>(view->wrap_cols(area.cols)) : std::nullopt);
        view->scroll_to_cursor(area.rows, area.cols);
        view->render(screen, area, std::nullopt, focused);
    }
    void at(std::uint64_t pos) { ed->select_range(pos, pos); }
    std::string row(int r) const {
        std::string out;
        for (int c = 0; c < screen.cols(); ++c) {
            const Cell& cell = screen.cell(r, c);
            out.append(cell.utf8.data(), cell.len);
        }
        while (!out.empty() && out.back() == ' ') out.pop_back();
        return out;
    }
    std::string glyph(int r, int c) const {
        const Cell& cell = screen.cell(r, c);
        return std::string(cell.utf8.data(), cell.len);
    }
};

const Attr kMarker = attr_for(Style::overflow_marker);

}  // namespace

TEST_CASE("the overflow marker looks like the focused status line, not like text") {
    CHECK(kMarker == attr_for(Style::status));
    CHECK(kMarker != attr_for(Style::Default));
}

TEST_CASE("wrap off: a line that runs past the right edge ends in a highlighted >") {
    //                       20 columns: 01234567890123456789
    Fixture f("marker.txt", "0123456789012345678901234\nabcdefghijklmnopqrst\nshort\n", 5, 20);
    f.draw();
    CHECK(f.row(0) == "0123456789012345678>");  // the last column gives way to the marker
    CHECK(f.screen.cell(0, 19).attr == kMarker);
    CHECK(f.screen.cell(0, 18).attr != kMarker);
    CHECK(f.row(1) == "abcdefghijklmnopqrst");  // exactly as wide as the area: nothing is hidden
    CHECK(f.screen.cell(1, 19).attr != kMarker);
    CHECK(f.row(2) == "short");
    CHECK(f.screen.cell(2, 19).attr != kMarker);
}

TEST_CASE("wrap off: the marker goes once the rest of the line is scrolled into view") {
    Fixture f("marker_scroll.txt", "0123456789012345678901234\n", 3, 20);
    f.at(25);  // the end of the line: the view scrolls right
    f.draw();
    CHECK(f.view->hscroll() > 0);
    CHECK(f.screen.cell(0, 19).attr != kMarker);
    CHECK(f.row(0).find('>') == std::string::npos);
}

TEST_CASE("wrap off: the marker sits at the right edge with a gutter, and a cut wide character counts") {
    Fixture f("marker_gutter.txt", "0123456789012345678901234\nab\n", 3, 20, true);
    f.draw();
    CHECK(f.row(0) == "  1 012345678901234>");
    CHECK(f.screen.cell(0, 19).attr == kMarker);
    CHECK(f.row(1) == "  2 ab");

    // Nine columns of text, then a wide character that needs columns 9 and 10 of a 10-column area.
    Fixture w("marker_wide.txt", "012345678\xE6\x97\xA5\n", 2, 10);
    w.draw();
    CHECK(w.glyph(0, 9) == ">");
    CHECK(w.screen.cell(0, 9).attr == kMarker);
}

TEST_CASE("wrap on: a long line continues on the next rows, broken at words, with no marker") {
    Fixture f("wrap.txt", "aaaa bbbb cccc dddd\nxx\n", 6, 10);
    f.view->set_word_wrap(true);
    CHECK(f.view->word_wrap());
    CHECK(f.view->wrap_cols(10) == 9);  // one column is kept for the cursor at a row's end
    f.draw();
    CHECK(f.row(0) == "aaaa bbbb");
    CHECK(f.row(1) == "cccc dddd");
    CHECK(f.row(2) == "xx");
    CHECK(f.row(3).empty());
    for (int r = 0; r < 6; ++r) {
        for (int c = 0; c < 10; ++c) CHECK(f.screen.cell(r, c).attr != kMarker);
    }
    CHECK(f.view->hscroll() == 0);
}

TEST_CASE("wrap on: the gutter numbers a line's first row only") {
    Fixture f("wrap_gutter.txt", "aaaa bbbb cccc dddd\nxx\n", 6, 14, true);  // a 4-column gutter, 10 for text
    f.view->set_word_wrap(true);
    f.draw();
    CHECK(f.row(0) == "  1 aaaa bbbb");
    CHECK(f.row(1) == "    cccc dddd");
    CHECK(f.row(2) == "  2 xx");
    CHECK(f.row(3) == "  3");
    CHECK(f.row(4) == "  ~");
}

TEST_CASE("wrap on: the cursor is drawn on its row, and Down moves one screen row") {
    Fixture f("wrap_cursor.txt", "aaaa bbbb cccc dddd\nxx\n", 6, 10);
    f.view->set_word_wrap(true);
    f.at(2);
    f.draw();
    CHECK(f.screen.cursor_visible());
    CHECK(f.screen.cursor_row() == 0);
    CHECK(f.screen.cursor_col() == 2);
    f.ed->move(Motion::Down);
    f.draw();
    CHECK(f.ed->cursor() == 12);
    CHECK(f.screen.cursor_row() == 1);
    CHECK(f.screen.cursor_col() == 2);
    f.at(19);  // the end of the line stays on the line's last row
    f.draw();
    CHECK(f.screen.cursor_row() == 1);
    CHECK(f.screen.cursor_col() == 9);
    f.screen.set_cursor(0, 0, false);  // as App does before drawing
    f.draw(false);                     // without focus the view leaves the cursor alone
    CHECK_FALSE(f.screen.cursor_visible());
}

TEST_CASE("wrap on: a selection is shown on every row it covers") {
    Fixture f("wrap_sel.txt", "aaaa bbbb cccc dddd\nxx\n", 6, 10);
    f.view->set_word_wrap(true);
    f.ed->select_range(7, 12);
    f.draw();
    const Attr sel = attr_for(Style::selection);
    CHECK(f.screen.cell(0, 6).attr != sel);
    CHECK(f.screen.cell(0, 7).attr == sel);
    CHECK(f.screen.cell(1, 0).attr == sel);
    CHECK(f.screen.cell(1, 1).attr == sel);
    CHECK(f.screen.cell(1, 2).attr != sel);
}

TEST_CASE("wrap on: scrolling works by rows inside one very long line") {
    std::string line;
    for (int i = 0; i < 300; ++i) line += "word" + std::to_string(i) + " ";
    Fixture f("wrap_long.txt", line + "\nend\n", 5, 20);
    f.view->set_word_wrap(true);
    f.draw();
    CHECK(f.view->top() == 0);
    f.at(line.size());  // the end of the long line
    f.draw();
    const WrapLayout wrap(f.doc->text(), f.view->wrap_cols(20), 4);
    CHECK(f.view->top() > 0);
    CHECK(f.view->top() < line.size());                   // still inside the same line
    CHECK(wrap.row_start(f.view->top()) == f.view->top());  // and exactly on a row
    CHECK(f.screen.cursor_visible());
    CHECK(f.screen.cursor_row() < 5);
    // One row up from the top row scrolls by exactly one row.
    const std::uint64_t top_before = f.view->top();
    for (int i = 0; i < 5; ++i) f.ed->move(Motion::Up);
    f.draw();
    CHECK(f.view->top() < top_before);
    CHECK(f.screen.cursor_visible());
    f.at(0);
    f.draw();
    CHECK(f.view->top() == 0);
    CHECK(f.row(0) == "word0 word1 word2");
}

TEST_CASE("turning wrap off brings back sideways scrolling from the line's start") {
    std::string line;
    for (int i = 0; i < 60; ++i) line += "word" + std::to_string(i) + " ";
    Fixture f("wrap_toggle.txt", line + "\nend\n", 5, 20);
    f.view->set_word_wrap(true);
    f.at(line.size());
    f.draw();
    REQUIRE(f.view->top() > 0);
    f.view->set_word_wrap(false);
    f.draw();
    CHECK(f.view->top() == 0);      // one row per line again
    CHECK(f.view->hscroll() > 0);   // and the long line is scrolled sideways to the cursor
    f.view->set_word_wrap(true);
    f.draw();
    CHECK(f.view->hscroll() == 0);
}

TEST_CASE("wrap on: an edit above the view keeps the same text at the top") {
    std::string line;
    for (int i = 0; i < 300; ++i) line += "word" + std::to_string(i) + " ";
    Fixture f("wrap_edit.txt", "first\n" + line + "\n", 5, 20);
    f.view->set_word_wrap(true);
    f.at(6 + line.size());
    f.draw();
    const std::uint64_t top = f.view->top();
    const std::string top_row = f.row(0);
    f.doc->apply(0, 0, std::string_view("ab"), EditKind::typing, 0, 2);  // two bytes before everything
    f.draw();
    CHECK(f.view->top() == top + 2);
    CHECK(f.row(0) == top_row);
}

TEST_CASE("set_top shows a given line first; the status line marks a read-only view") {
    Fixture f("settop.txt", "l1\nl2\nl3\nl4\nl5\nl6\n", 3, 20);
    f.view->set_top(7);  // inside "l3": the line's start
    CHECK(f.view->top() == 6);
    f.view->render(f.screen, f.area, std::nullopt, false);
    CHECK(f.row(0) == "l3");
    f.screen.resize({4, 40});
    f.view->render_status(f.screen, 3, "");
    CHECK(f.row(3).find("settop.txt  ") != std::string::npos);
    CHECK(f.row(3).find("[view]") == std::string::npos);
    f.view->set_read_only(true);
    f.view->render_status(f.screen, 3, "");
    CHECK(f.row(3).find("settop.txt [view]") != std::string::npos);
}

TEST_CASE("history kept in memory is not news on the status line, nor in a read-only view") {
    Fixture f("rostatus.txt", "abc\n", 2, 60, false, false);  // no history file: kept in memory
    f.view->render_status(f.screen, 1, "");
    CHECK(f.row(1).find("history:") == std::string::npos);
    f.view->set_read_only(true);
    f.view->render_status(f.screen, 1, "");
    CHECK(f.row(1).find("history:") == std::string::npos);
}

TEST_CASE("the theme's new defaults: variables, properties, parameters, constants and the new modifiers") {
    using namespace theme_color;
    CHECK(attr_for(Style::lsp_variable) == Attr{cyan + bright, kDefaultColor, 0});
    CHECK(attr_for(Style::lsp_property) == Attr{blue + bright, kDefaultColor, 0});
    CHECK(attr_for(Style::lsp_parameter) == Attr{cyan + bright, kDefaultColor, kItalic});
    CHECK(attr_for(Style::constant) == Attr{yellow, kDefaultColor, 0});
    CHECK(attr_for(Style::lsp_function, kModDeclaration).flags == kBold);
    CHECK(attr_for(Style::lsp_function, kModDefaultLibrary).flags == kItalic);
    CHECK(attr_for(Style::lsp_variable, kModReadonly).fg == cyan + bright);  // already bright: unchanged
    CHECK(attr_for(Style::lsp_function, kModReadonly).fg == blue + bright);
}

TEST_CASE("the status line centers its message between the name and the position") {
    Fixture f("center.txt", "abc\n", 2, 60);
    f.view->render_status(f.screen, 1, "hello");
    const std::string row = f.row(1);
    CHECK(row.starts_with(" center.txt"));
    CHECK(row.find("hello") == (60 - 5) / 2);
    CHECK(row.find("1:1") != std::string::npos);
    // Too long to fit between the blocks: the position block gives way.
    const std::string longer(50, 'm');
    f.view->render_status(f.screen, 1, longer);
    const std::string r2 = f.row(1);
    CHECK(r2.find("1:1") == std::string::npos);
    CHECK(r2.find("mmmm") == std::string(" center.txt  ").size());
}


TEST_CASE("paper: the text area is drawn on the page, the overflow marker is not") {
    Fixture f("paper.txt", "abcdefghijklmnopqrstuvwxyz\n", 2, 20);
    active_theme().set_darkness(Darkness::paper);
    f.view->render(f.screen, f.area, std::nullopt, false);
    const Attr page = attr_for(Style::page);
    CHECK(f.screen.cell(0, 0).attr.bg == page.bg);   // text
    CHECK(f.screen.cell(1, 5).attr == page);         // past the end of the text
    CHECK(f.screen.cell(0, 19).attr == attr_for(Style::overflow_marker));
    active_theme().set_darkness(Darkness::normal);
}

TEST_CASE("preview mode draws the inserted text highlighted and the removed text struck, with no selection") {
    Fixture f("preview.txt", "abcdef\n", 2, 20);
    f.view->set_preview(std::vector<PreviewMark>{{1, 2, true}, {3, 5, false}});
    f.view->render(f.screen, f.area, std::nullopt, false);
    CHECK(f.screen.cell(0, 0).attr == attr_for(Style::Default));
    CHECK(f.screen.cell(0, 1).attr == attr_for(Style::history_removed));
    CHECK(f.screen.cell(0, 3).attr == attr_for(Style::history_inserted));
    CHECK(f.screen.cell(0, 4).attr == attr_for(Style::history_inserted));
    CHECK(f.screen.cell(0, 5).attr == attr_for(Style::Default));
    f.view->set_preview(std::nullopt);
    f.view->render(f.screen, f.area, std::nullopt, false);
    CHECK(f.screen.cell(0, 3).attr == attr_for(Style::Default));
}

TEST_CASE("reading: a Markdown document is drawn laid out, marks hidden, the cursor where its source byte is shown") {
    //                      0123456 7 8901234567890123456789
    const std::string src = "# Head\n\nSome **bold** text.\n";
    Fixture f("reading.md", src, 6, 40);
    f.view->set_reading(true);
    f.at(15);  // "b" of bold
    f.draw();
    CHECK(f.row(0) == "Head");
    CHECK(f.row(1) == "════");
    CHECK(f.row(3) == "Some bold text.");
    CHECK(f.screen.cursor_visible());
    CHECK(f.screen.cursor_row() == 3);
    CHECK(f.screen.cursor_col() == 5);
    REQUIRE(f.view->reading_layout() != nullptr);
    f.view->set_reading(false);
    f.draw();
    CHECK(f.row(0) == "# Head");
    CHECK(f.view->reading_layout() == nullptr);
}

TEST_CASE("reading: the selection is drawn over the shown bytes of its source range") {
    const std::string src = "Some **bold** text.\n";
    Fixture f("reading_sel.md", src, 3, 40);
    f.view->set_reading(true);
    f.ed->select_range(5, 13);  // "**bold**"
    f.draw();
    CHECK(f.row(0) == "Some bold text.");
    const Attr sel = attr_for(Style::selection);
    CHECK((f.screen.cell(0, 5).attr.flags & sel.flags) == sel.flags);
    CHECK((f.screen.cell(0, 8).attr.flags & sel.flags) == sel.flags);
    CHECK((f.screen.cell(0, 10).attr.flags & sel.flags) == 0);
}

TEST_CASE("reading: the gutter numbers each source line once; a narrow area draws the lines as usual") {
    const std::string src = "alpha beta gamma delta epsilon zeta\n\nnext\n";
    Fixture f("reading_gutter.md", src, 6, 30, true);
    f.view->set_reading(true);
    f.draw();
    CHECK(f.row(0).starts_with("  1 alpha"));
    CHECK(f.row(1).starts_with("    "));  // the paragraph's second rendered line: no number
    CHECK(f.row(2).empty());
    CHECK(f.row(3).starts_with("  3 next"));
    Fixture narrow("reading_narrow.md", "**x**\n", 3, 15);
    narrow.view->set_reading(true);
    narrow.draw();
    CHECK(narrow.row(0) == "**x**");  // under 21 columns of text: not laid out
    CHECK(narrow.view->reading_layout() == nullptr);
}

TEST_CASE("reading: scrolling follows the cursor by rendered line") {
    std::string src;
    for (int i = 0; i < 30; ++i) src += "Line **" + std::to_string(i) + "**\n\n";
    Fixture f("reading_scroll.md", src, 5, 40);
    f.view->set_reading(true);
    f.at(src.find("29"));
    f.draw();
    CHECK(f.screen.cursor_visible());
    bool shown = false;
    for (int r = 0; r < 5; ++r) shown = shown || f.row(r) == "Line 29";
    CHECK(shown);
}

TEST_CASE("reading: a wide table pans sideways to keep the cursor on screen, with > on cut lines") {
    const std::string wide(60, 'w');
    const std::string src = "| a | b |\n|---|---|\n| x | " + wide + " |\n";
    Fixture f("reading_pan.md", src, 4, 30);
    f.view->set_reading(true);
    f.at(src.find("x"));
    f.draw();
    CHECK(f.row(2).starts_with("x │ www"));
    CHECK(f.glyph(2, 29) == ">");  // the row runs past the edge
    CHECK(f.screen.cell(2, 29).attr == attr_for(Style::overflow_marker));
    CHECK(f.row(0) == "a │ b");  // a short row: no marker
    // End: the cursor goes after the last w, and the view pans to show it.
    f.at(src.find(wide) + wide.size());
    f.draw();
    REQUIRE(f.screen.cursor_visible());
    CHECK(f.screen.cursor_row() == 2);
    CHECK(f.screen.cursor_col() < 30);
    CHECK(f.glyph(2, f.screen.cursor_col() - 1) == "w");  // the end of the row is shown
    CHECK(f.glyph(2, 0) != "x");                          // panned
    // Home: back to column 0.
    f.at(src.find("x"));
    f.draw();
    CHECK(f.row(2).starts_with("x │ www"));
    CHECK(f.screen.cursor_col() == 0);
}

TEST_CASE("a text area a column or two wide draws, with line numbers, in every mode") {
    for (int cols : {1, 2, 3}) {
        for (bool wrap : {false, true}) {
            CAPTURE(cols);
            CAPTURE(wrap);
            Fixture f("narrow" + std::to_string(cols) + (wrap ? "w" : "") + ".txt", "ab\n", 4, cols, true);
            f.view->set_word_wrap(wrap);
            CHECK_NOTHROW(f.draw());
            f.view->set_reading(true);  // too narrow to lay out: drawn as lines
            CHECK_NOTHROW(f.draw());
        }
    }
}

TEST_CASE("moving along a 4 MB line costs a frame no more than a short line does") {
    const std::string line(4u << 20, 'x');
    Fixture f("long_line.txt", line + "\n", 10, 80);
    f.ed->select_range(line.size() - 40, line.size() - 40);
    f.draw();
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < 30; ++i) {
        f.ed->move(Motion::Right);
        f.draw();
        f.view->render_status(f.screen, 9, "");
    }
    const auto seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    CHECK(seconds < time_budget(5.0));  // the same on a 1 MB or a 16 MB line; 213 s once
    CHECK(f.row(9).find(std::format("1:{}", line.size() - 9)) != std::string::npos);
}

TEST_CASE("with word wrap the status line counts the column from the line's start, not the row's") {
    Fixture f("wrap_status.txt", "aaaaaaaaa bbbbbbbbb ccccccccc ddddddddd eeeeeeeee\n", 6, 40);
    f.view->set_word_wrap(true);
    f.ed->select_range(45, 45);  // on the second row
    f.draw();
    f.view->render_status(f.screen, 5, "");
    CAPTURE(f.row(5));
    CHECK(f.row(5).find("1:46") != std::string::npos);
}

TEST_CASE("with several splits the focused status line starts with '>' and the others are on a darker band") {
    Fixture f("marks.txt", "one\n", 3, 30);
    f.view->render_status(f.screen, 2, "", StatusMark::focused);
    CHECK(f.row(2).starts_with(">marks.txt"));
    CHECK((f.screen.cell(2, 5).attr.flags & kDim) == 0);
    f.view->render_status(f.screen, 2, "", StatusMark::unfocused);
    CHECK(f.row(2).starts_with(" marks.txt"));
    for (int c = 0; c < 30; ++c) CHECK(f.screen.cell(2, c).attr == active_theme().unfocused_status());
    f.view->render_status(f.screen, 2, "");  // one view: neither
    CHECK(f.row(2).starts_with(" marks.txt"));
    CHECK((f.screen.cell(2, 5).attr.flags & kDim) == 0);
}

TEST_CASE("scroll_to keeps a place on screen and out of the margin rows, moving the least it can") {
    std::string text;
    for (int i = 0; i < 50; ++i) text += "line " + std::to_string(i) + "\n";
    for (const bool wrap : {false, true}) {
        CAPTURE(wrap);
        Fixture f(wrap ? "scroll_to_wrap.txt" : "scroll_to.txt", text, 10, 40);
        f.view->set_word_wrap(wrap);
        auto start = [&](int line) { return static_cast<std::uint64_t>(text.find("line " + std::to_string(line) + "\n")); };
        auto top_line = [&] { return f.view->top() == 0 ? 0 : static_cast<int>(std::stoi(text.substr(f.view->top() + 5))); };
        f.view->set_top(0);
        f.view->scroll_to(start(30), 10, 40, 2, true);  // below: two rows stay under it
        CHECK(top_line() == 23);
        f.view->scroll_to(start(26), 10, 40, 2, true);  // on screen, clear of the margins: no move
        CHECK(top_line() == 23);
        f.view->scroll_to(start(31), 10, 40, 2, true);  // in the bottom two rows: the least move
        CHECK(top_line() == 24);
        f.view->scroll_to(start(25), 10, 40, 2, true);  // in the top two rows
        CHECK(top_line() == 23);
        f.view->scroll_to(start(5), 10, 40, 2, true);   // above
        CHECK(top_line() == 3);
    }
}

TEST_CASE("a cut line's overflow marker takes its split's status look: focused, or unfocused") {
    Fixture f("marker.txt", std::string(60, 'x') + "\n", 4, 20);
    f.view->set_word_wrap(false);
    f.draw();
    CHECK(f.row(0).ends_with(">"));
    CHECK(f.screen.cell(0, 19).attr == attr_for(Style::overflow_marker));
    f.view->set_split_focused(false);
    f.draw();
    CHECK(f.screen.cell(0, 19).attr == active_theme().unfocused_status());
    f.view->set_split_focused(true);
    f.draw();
    CHECK(f.screen.cell(0, 19).attr == attr_for(Style::overflow_marker));
}
