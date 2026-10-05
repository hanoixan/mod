#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include "edit/clipboard.hpp"
#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "edit/sidecar.hpp"
#include "text/utf8.hpp"
#include "util/event_queue.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

struct BreakCase {
    std::string_view text;
    std::vector<std::uint32_t> boundaries;
};

// Unicode's own grapheme-break conformance cases.
const std::vector<BreakCase>& break_cases() {
    static const std::vector<BreakCase> cases = {
#include "grapheme_break_cases.inc"
    };
    return cases;
}

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "editor_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    fs::remove(sidecar_path_for(p));
    return p;
}

void write_file(const fs::path& p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string first_line(const fs::path& p) {
    std::ifstream in(p);
    std::string line;
    std::getline(in, line);
    return line;
}

template <class F>
bool pump(EventQueue& q, F done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (!done()) {
        if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        if (std::chrono::steady_clock::now() > deadline) return false;
    }
    return true;
}

// A document opened from a file holding `text`, with an editor and a clipboard.
struct Fixture {
    EventQueue q{{}};
    std::vector<std::string> terminal;
    Clipboard clipboard{[this](std::string_view s) { terminal.emplace_back(s); }};
    std::unique_ptr<Document> doc;
    std::unique_ptr<Editor> ed;

    Fixture(const std::string& name, std::string_view text, int tab_width = 4) {
        const fs::path p = scratch(name);
        write_file(p, text);
        auto d = Document::open(p, q);
        REQUIRE(d);
        doc = std::move(*d);
        ed = std::make_unique<Editor>(*doc, clipboard, tab_width);
    }
    ~Fixture() { ed.reset(); }

    std::string text() const { return doc->text().read(0, doc->text().size()); }
    void at(std::uint64_t pos) { ed->select_range(pos, pos); }
    std::vector<std::uint64_t> walk_right() {
        std::vector<std::uint64_t> out{ed->cursor()};
        while (ed->cursor() < doc->text().size()) {
            ed->move(Motion::Right);
            out.push_back(ed->cursor());
        }
        return out;
    }
    std::vector<std::uint64_t> walk_left() {
        std::vector<std::uint64_t> out{ed->cursor()};
        while (ed->cursor() > 0) {
            ed->move(Motion::Left);
            out.push_back(ed->cursor());
        }
        return out;
    }
};

const std::string kFamily = "\xF0\x9F\x91\xA8\xE2\x80\x8D\xF0\x9F\x91\xA9\xE2\x80\x8D\xF0\x9F\x91\xA7";  // 18 bytes
const std::string kThumbTone = "\xF0\x9F\x91\x8D\xF0\x9F\x8F\xBD";                                       // 8
const std::string kFlagFr = "\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7";                                          // 8
const std::string kRi = "\xF0\x9F\x87\xA6";                                                               // 4
const std::string kEAcute = "e\xCC\x81";                                                                  // 3
const std::string kHangulLvt = "\xE1\x84\x80\xE1\x85\xA1\xE1\x86\xA8";                                    // 9
const std::string kKsha = "\xE0\xA4\x95\xE0\xA5\x8D\xE0\xA4\xB7";                                         // 9, GB9c

}  // namespace

TEST_CASE("the conformance table and the width table share one Unicode version") {
    const fs::path tests = fs::path(__FILE__).parent_path();
    const std::string a = first_line(tests / "grapheme_break_cases.inc");
    const std::string b = first_line(tests.parent_path() / "src" / "text" / "width_table.inc");
    const auto version = [](const std::string& s) { return s.substr(s.find("Unicode ")); };
    REQUIRE(a.find("Unicode ") != std::string::npos);
    REQUIRE(b.find("Unicode ") != std::string::npos);
    CHECK(version(a) == version(b));
}

TEST_CASE("UAX #29 conformance through both boundary functions") {
    std::size_t checked = 0;
    for (const BreakCase& c : break_cases()) {
        const auto bytes = std::as_bytes(std::span(c.text.data(), c.boundaries.back()));
        const auto& b = c.boundaries;
        // Forward from every boundary.
        for (std::size_t i = 0; i + 1 < b.size(); ++i) {
            const ClusterResult r = next_grapheme_boundary(bytes.subspan(b[i]), true);
            CHECK_MESSAGE(r.length == b[i + 1] - b[i], "case #" << checked << " forward at " << b[i]);
        }
        // Backward from every boundary.
        for (std::size_t i = b.size() - 1; i > 0; --i) {
            const ClusterResult r = prev_grapheme_boundary(bytes.first(b[i]), true);
            CHECK_MESSAGE(r.length == b[i] - b[i - 1], "case #" << checked << " backward at " << b[i]);
        }
        ++checked;
    }
    CHECK(checked > 1000);
}

TEST_CASE("Left, Right, Backspace and Delete work on extended grapheme clusters") {
    const std::string text = "a" + kFamily + "b" + kThumbTone + kFlagFr + kRi + kRi + kRi + kEAcute + kHangulLvt + kKsha +
                             "\r\n" + "\xFF\xFE" + "z";
    std::vector<std::uint64_t> stops{0};
    for (std::size_t len : {1, 18, 1, 8, 8, 8, 4, 3, 9, 9, 2, 1, 1, 1}) stops.push_back(stops.back() + len);
    REQUIRE(stops.back() == text.size());
    Fixture f("clusters.txt", text);

    CHECK(f.walk_right() == stops);
    std::vector<std::uint64_t> back(stops.rbegin(), stops.rend());
    CHECK(f.walk_left() == back);

    SUBCASE("Backspace removes whole clusters") {
        f.at(text.size());
        for (std::size_t i = stops.size() - 1; i > 0; --i) {
            f.ed->delete_backward();
            CHECK(f.doc->text().size() == stops[i - 1]);
        }
    }
    SUBCASE("Delete removes whole clusters") {
        f.at(0);
        for (std::size_t i = 1; i < stops.size(); ++i) {
            f.ed->delete_forward();
            CHECK(f.doc->text().size() == text.size() - stops[i]);
        }
    }
}

TEST_CASE("clusters that span pieces") {
    // The file holds the first half of the family emoji; typing adds the rest.
    Fixture f("span.txt", kFamily.substr(0, 11));
    f.at(11);
    f.ed->insert_text(kFamily.substr(11) + "x");
    CHECK(f.text() == kFamily + "x");
    f.at(0);
    f.ed->move(Motion::Right);
    CHECK(f.ed->cursor() == 18);
    f.ed->move(Motion::Left);
    CHECK(f.ed->cursor() == 0);
    f.at(18);
    f.ed->delete_backward();
    CHECK(f.text() == "x");
}

TEST_CASE("the cluster cap of 32 code points") {
    std::string marks31 = "a";
    for (int i = 0; i < 31; ++i) marks31 += "\xCC\x81";
    std::string marks32 = marks31 + "\xCC\x81";
    {
        Fixture f("cap31.txt", marks31);
        CHECK(f.walk_right() == std::vector<std::uint64_t>{0, 63});
        CHECK(f.walk_left() == std::vector<std::uint64_t>{63, 0});
    }
    {
        Fixture f("cap32.txt", marks32);
        CHECK(f.walk_right() == std::vector<std::uint64_t>{0, 63, 65});
        CHECK(f.walk_left() == std::vector<std::uint64_t>{65, 63, 0});
    }
}

TEST_CASE("every motion, with and without Shift") {
    Fixture f("motions.txt", "one two\nthree\n\nfour");
    Editor& e = *f.ed;
    e.move(Motion::DocEnd);
    CHECK(e.cursor() == 19);
    e.move(Motion::DocStart, true);
    CHECK(e.selection() == std::pair<std::uint64_t, std::uint64_t>{0, 19});
    e.move(Motion::Right);  // collapses to the selection's end
    CHECK(e.cursor() == 19);
    CHECK_FALSE(e.selection());
    e.move(Motion::DocStart);
    e.move(Motion::Right, true);
    e.move(Motion::Right, true);
    CHECK(e.selection() == std::pair<std::uint64_t, std::uint64_t>{0, 2});
    e.move(Motion::Left);  // collapses to the start
    CHECK(e.cursor() == 0);
    e.move(Motion::LineEnd);
    CHECK(e.cursor() == 7);
    e.move(Motion::LineStart, true);
    CHECK(e.anchor() == std::optional<std::uint64_t>(7));
    CHECK(e.cursor() == 0);
    e.move(Motion::WordRight);
    CHECK(e.cursor() == 3);
    e.move(Motion::WordRight, true);
    CHECK(e.selection() == std::pair<std::uint64_t, std::uint64_t>{3, 7});
    e.move(Motion::WordLeft);
    CHECK(e.cursor() == 4);
    e.move(Motion::Down);
    CHECK(e.cursor() == 12);  // column 4 of "three"
    e.move(Motion::Down);
    CHECK(e.cursor() == 14);  // the empty line
    e.move(Motion::Down);
    CHECK(e.cursor() == 19);  // column 4 of "four", the sticky column
    e.move(Motion::Up, true);
    CHECK(e.selection() == std::pair<std::uint64_t, std::uint64_t>{14, 19});
    e.move(Motion::PageUp, false, 2);
    CHECK(e.cursor() == 4);
    e.move(Motion::PageDown, true, 3);
    CHECK(e.cursor() == 19);
    CHECK(e.anchor() == std::optional<std::uint64_t>(4));
    e.move(Motion::Up);
    e.move(Motion::Up);
    e.move(Motion::Left);
    e.move(Motion::Up);
    CHECK(e.cursor() == 3);  // the sticky column was reset by Left
}

TEST_CASE("word motion: Unicode words, punctuation runs, and line feeds as stops") {
    Fixture f("words.txt", "foo_bar, baz  qux\nnext h\xC3\xA9llo w\xC3\xB6rld");
    Editor& e = *f.ed;
    std::vector<std::uint64_t> right;
    while (e.cursor() < f.doc->text().size()) {
        e.move(Motion::WordRight);
        right.push_back(e.cursor());
    }
    CHECK(right == std::vector<std::uint64_t>{7, 8, 12, 17, 18, 22, 29, 36});
    std::vector<std::uint64_t> left;
    while (e.cursor() > 0) {
        e.move(Motion::WordLeft);
        left.push_back(e.cursor());
    }
    CHECK(left == std::vector<std::uint64_t>{30, 23, 18, 17, 14, 9, 7, 0});

    SUBCASE("Ctrl+Backspace and Ctrl+Delete use the same stops") {
        f.at(12);
        e.delete_backward(true);
        CHECK(f.text().substr(0, 12) == "foo_bar,   q");
        e.delete_forward(true);
        CHECK(f.text().substr(0, 10) == "foo_bar, \n");
    }
}

TEST_CASE("the sticky column across wide characters and tabs") {
    const std::string text = "abcdef\n\xE4\xB8\x96\xE7\x95\x8Cx\n\tz\nab";  // 世界
    SUBCASE("tab width 4 (the default)") {
        Fixture f("sticky4.txt", text);
        Editor& e = *f.ed;
        f.at(4);
        e.move(Motion::Down);
        CHECK(e.cursor() == 13);  // after 世界, column 4
        e.move(Motion::Down);
        CHECK(e.cursor() == 16);  // after the tab, column 4
        e.move(Motion::Down);
        CHECK(e.cursor() == 20);  // the end of "ab"
        e.move(Motion::Up);
        CHECK(e.cursor() == 16);
        e.move(Motion::Up);
        CHECK(e.cursor() == 13);
        e.move(Motion::Up);
        CHECK(e.cursor() == 4);
        f.at(2);
        e.move(Motion::Down);
        CHECK(e.cursor() == 10);  // after 世
        e.move(Motion::Down);
        CHECK(e.cursor() == 16);  // the tab reaches column 4 >= 2
    }
    SUBCASE("tab width 1") {
        Fixture f("sticky1.txt", text, 1);
        f.at(2);
        f.ed->move(Motion::Down);
        f.ed->move(Motion::Down);
        CHECK(f.ed->cursor() == 17);  // the tab is 1 column, "z" reaches 2
    }
    SUBCASE("tab width 8, and after set_tab_width") {
        Fixture f("sticky8.txt", "\tx\n0123456789\n", 8);
        f.at(1);  // after the tab: column 8
        f.ed->move(Motion::Down);
        CHECK(f.ed->cursor() == 11);
        f.ed->set_tab_width(2);
        f.at(1);
        f.ed->move(Motion::Down);
        CHECK(f.ed->cursor() == 5);
        CHECK(f.ed->tab_width() == 2);
    }
}

TEST_CASE("with a wrap width, Up, Down and the page keys move by screen row") {
    // At 10 columns: "aaaa bbbb " (0-10) | "cccc dddd" (10-19), then the line "xx" (20-22).
    Fixture f("wraprows.txt", "aaaa bbbb cccc dddd\nxx");
    Editor& e = *f.ed;
    e.set_wrap_width(10);

    SUBCASE("Down and Up step through the rows of one line, keeping the column in the row") {
        f.at(2);
        e.move(Motion::Down);
        CHECK(e.cursor() == 12);  // column 2 of the second row
        e.move(Motion::Down);
        CHECK(e.cursor() == 22);  // the next line
        e.move(Motion::Down);
        CHECK(e.cursor() == 22);  // the last row: nowhere to go
        e.move(Motion::Up);
        CHECK(e.cursor() == 12);
        e.move(Motion::Up);
        CHECK(e.cursor() == 2);
        e.move(Motion::Up);
        CHECK(e.cursor() == 2);  // the first row
    }
    SUBCASE("the sticky column survives a short row") {
        f.at(7);
        e.move(Motion::Down);
        CHECK(e.cursor() == 17);
        e.move(Motion::Down);
        CHECK(e.cursor() == 22);  // "xx" is shorter: its end
        e.move(Motion::Up);
        CHECK(e.cursor() == 17);  // back at column 7
    }
    SUBCASE("a position on a row that is not the line's last never lands on the next row's start") {
        f.at(19);  // the end of the line: column 9 of the second row
        e.move(Motion::Up);
        CHECK(e.cursor() == 9);
        Fixture g("wrapfull.txt", "bbbbbbbbbb");
        g.ed->set_wrap_width(5);  // "bbbbb" | "bbbbb"
        g.at(10);                 // column 5: after the last character of the last row
        g.ed->move(Motion::Up);
        CHECK(g.ed->cursor() == 4);  // the last character of the first row, not offset 5
        g.ed->move(Motion::Down);
        CHECK(g.ed->cursor() == 10);
    }
    SUBCASE("PageDown and PageUp count rows; Shift extends") {
        f.at(1);
        e.move(Motion::PageDown, false, 2);
        CHECK(e.cursor() == 21);
        e.move(Motion::PageUp, true, 2);
        CHECK(e.cursor() == 1);
        REQUIRE(e.selection().has_value());
        CHECK(*e.selection() == std::pair<std::uint64_t, std::uint64_t>{1, 21});
    }
    SUBCASE("Home and End stay whole-line") {
        f.at(12);
        e.move(Motion::LineStart);
        CHECK(e.cursor() == 0);
        e.move(Motion::LineEnd);
        CHECK(e.cursor() == 19);
    }
    SUBCASE("without a wrap width the same keys move by line again") {
        e.set_wrap_width(std::nullopt);
        f.at(2);
        e.move(Motion::Down);
        CHECK(e.cursor() == 22);
        e.move(Motion::Up);
        CHECK(e.cursor() == 2);
    }
    SUBCASE("tab stops restart on each row") {
        Fixture g("wraptab.txt", "aaaaaaa b\tc\nxxxxxxxxxx");
        g.ed->set_wrap_width(8);  // "aaaaaaa " | "b\tc": the tab runs from column 1 to 4 of its row
        g.at(10);                 // on "c", column 4 of the second row
        g.ed->move(Motion::Down);
        CHECK(g.ed->cursor() == 16);  // column 4 of "xxxxxxxx" | "xx"
    }
}

TEST_CASE("display widths: tabs, C0 and C1 controls, invalid bytes, wide characters") {
    auto width_of = [](std::string_view s, std::uint64_t column = 0, int tab_width = 4) {
        const Decoded d = decode(std::as_bytes(std::span(s.data(), s.size())));
        return display_width(d, column, tab_width);
    };
    CHECK(width_of("\t", 0) == 4);
    CHECK(width_of("\t", 5) == 3);
    CHECK(width_of("\t", 5, 8) == 3);
    CHECK(width_of("\x01") == 2);       // ^A
    CHECK(width_of("\x7F") == 2);       // ^?
    CHECK(width_of("\xC2\x80") == 6);   // U+0080 as \u0080
    CHECK(width_of("\xC2\x9B") == 6);   // U+009B (CSI) is never sent raw
    CHECK(width_of("\xC2\x9F") == 6);
    CHECK(width_of("\xC2\xA0") == 1);   // U+00A0 is not a control
    CHECK(width_of("\x9B") == 4);       // a lone invalid byte as \x9B
    CHECK(width_of("a") == 1);
    CHECK(width_of("\xE4\xB8\x96") == 2);  // 世
    CHECK(width_of("\xCC\x81") == 0);       // combining acute
}

TEST_CASE("the cursor is never left between CR and LF") {
    Fixture f("crlf.txt", "abcd\nab\r\ncd");
    Editor& e = *f.ed;
    f.at(4);
    e.move(Motion::Down);
    CHECK(e.cursor() == 7);  // before the CR, not between CR and LF
    e.move(Motion::Right);
    CHECK(e.cursor() == 9);
    e.move(Motion::Left);
    CHECK(e.cursor() == 7);
    e.move(Motion::LineEnd);
    CHECK(e.cursor() == 7);
    f.at(9);
    e.delete_backward();
    CHECK(f.text() == "abcd\nabcd");
    REQUIRE(e.undo());
    CHECK(f.text() == "abcd\nab\r\ncd");
    CHECK(e.cursor() == 9);
}

TEST_CASE("copy, cut and paste use PieceRuns") {
    Fixture f("clip.txt", "hello world\n");
    Editor& e = *f.ed;

    SUBCASE("with no selection they do nothing") {
        const NodeId before = *f.doc->history().current();
        CHECK_FALSE(e.copy());
        e.cut();
        CHECK(f.clipboard.get() == nullptr);
        CHECK(f.terminal.empty());
        CHECK(*f.doc->history().current() == before);
        CHECK(f.text() == "hello world\n");
        e.paste();  // an empty clipboard
        CHECK(f.text() == "hello world\n");
    }
    SUBCASE("copy and paste") {
        e.select_range(0, 5);
        CHECK(e.copy());
        const ClipContent* c = f.clipboard.get();
        REQUIRE(c != nullptr);
        CHECK(c->length == 5);
        REQUIRE(c->run.size() == 1);
        CHECK(c->run[0].buffer == 1);  // the file's own mapping: no byte copy
        CHECK(c->source == f.doc->id());
        f.at(11);
        e.paste();
        CHECK(f.text() == "hello worldhello\n");
        CHECK(e.cursor() == 16);
        REQUIRE(e.undo());
        CHECK(f.text() == "hello world\n");
    }
    SUBCASE("cut is one node, and paste replaces a selection as one node") {
        e.select_range(5, 11);
        e.cut();
        CHECK(f.text() == "hello\n");
        CHECK(f.doc->history().meta(*f.doc->history().current()).kind == EditKind::cut);
        e.select_range(0, 5);
        e.paste();
        CHECK(f.text() == " world\n");
        REQUIRE(e.undo());
        CHECK(f.text() == "hello\n");
        REQUIRE(e.undo());
        CHECK(f.text() == "hello world\n");
    }
    SUBCASE("content copied from another document is pasted as a copy") {
        e.select_range(0, 5);
        e.copy();
        Fixture other("clip2.txt", "x");
        other.ed.reset();
        Editor ed2(*other.doc, f.clipboard);
        ed2.select_range(1, 1);
        ed2.paste();
        CHECK(other.text() == "xhello");
    }
}

TEST_CASE("text pasted from the terminal gets the document's line ending for every line break") {
    SUBCASE("a terminal sends each newline of a paste as a bare CR") {
        Fixture f("paste_cr.txt", "x\ny");
        Editor& e = *f.ed;
        f.at(0);
        e.paste_text("a\rb\r");
        CHECK(f.text() == "a\nb\nx\ny");
        CHECK(e.cursor() == 4);
        CHECK(f.doc->history().meta(*f.doc->history().current()).kind == EditKind::paste);
        REQUIRE(e.undo());  // one node
        CHECK(f.text() == "x\ny");
    }
    SUBCASE("CR LF and LF count as one break each") {
        Fixture f("paste_mixed.txt", "");
        f.ed->paste_text("a\r\nb\nc\rd\r\r\ne");
        CHECK(f.text() == "a\nb\nc\nd\n\ne");
    }
    SUBCASE("a CR LF document gets CR LF") {
        Fixture f("paste_crlf.txt", "one\r\ntwo");
        Editor& e = *f.ed;
        f.at(8);
        e.paste_text("\ra\nb\r\n");
        CHECK(f.text() == "one\r\ntwo\r\na\r\nb\r\n");
        CHECK(e.cursor() == f.doc->text().size());
    }
    SUBCASE("it replaces a selection as one node") {
        Fixture f("paste_sel.txt", "abcdef");
        Editor& e = *f.ed;
        e.select_range(1, 4);
        e.paste_text("X\rY");
        CHECK(f.text() == "aX\nYef");
        REQUIRE(e.undo());
        CHECK(f.text() == "abcdef");
    }
    SUBCASE("text with no line break is inserted as it is") {
        Fixture f("paste_plain.txt", "ab");
        f.at(1);
        f.ed->paste_text("\tq ");
        CHECK(f.text() == "a\tq b");
    }
}

namespace {
std::string clip_text(const Clipboard& c) {
    const ClipContent* content = c.get();
    if (content == nullptr) return "<empty>";
    std::string out;
    for (const FrozenBytes& v : content->views) out.append(reinterpret_cast<const char*>(v.bytes.data()), v.bytes.size());
    return out;
}
}  // namespace

TEST_CASE("cut to line end (Ctrl+K)") {
    SUBCASE("from the middle of a line, as one cut node") {
        Fixture f("kill_mid.txt", "hello world\nnext\n");
        f.at(5);
        f.ed->cut_to_line_end(false);
        CHECK(f.text() == "hello\nnext\n");
        CHECK(clip_text(f.clipboard) == " world");
        CHECK(f.ed->cursor() == 5);
        CHECK(f.doc->history().meta(*f.doc->history().current()).kind == EditKind::cut);
        REQUIRE(f.ed->undo());
        CHECK(f.text() == "hello world\nnext\n");
    }
    SUBCASE("at the end of a line it cuts the line break, joining the next line") {
        Fixture f("kill_lf.txt", "ab\ncd\n");
        f.at(2);
        f.ed->cut_to_line_end(false);
        CHECK(f.text() == "abcd\n");
        CHECK(clip_text(f.clipboard) == "\n");
    }
    SUBCASE("a CR LF break is cut whole, also from between nothing and the CR") {
        Fixture f("kill_crlf.txt", "ab\r\ncd");
        f.at(2);
        f.ed->cut_to_line_end(false);
        CHECK(f.text() == "abcd");
        CHECK(clip_text(f.clipboard) == "\r\n");
    }
    SUBCASE("at the end of the text it does nothing and keeps the clipboard") {
        Fixture f("kill_end.txt", "ab");
        f.at(0);
        f.ed->cut_to_line_end(false);
        CHECK(clip_text(f.clipboard) == "ab");
        const NodeId before = *f.doc->history().current();
        f.ed->cut_to_line_end(false);
        CHECK(f.text().empty());
        CHECK(clip_text(f.clipboard) == "ab");
        CHECK(*f.doc->history().current() == before);
    }
    SUBCASE("with a selection it cuts the selection") {
        Fixture f("kill_sel.txt", "abcdef\nx");
        f.ed->select_range(1, 3);
        f.ed->cut_to_line_end(false);
        CHECK(f.text() == "adef\nx");
        CHECK(clip_text(f.clipboard) == "bc");
    }
    SUBCASE("appending: consecutive presses collect every piece, one undo node each") {
        Fixture f("kill_append.txt", "one\ntwo\nthree");
        f.at(0);
        f.ed->cut_to_line_end(false);  // "one"
        f.ed->cut_to_line_end(true);   // "\n"
        f.ed->cut_to_line_end(true);   // "two"
        CHECK(f.text() == "\nthree");
        CHECK(clip_text(f.clipboard) == "one\ntwo");
        CHECK(f.clipboard.get()->length == 7);
        f.at(f.doc->text().size());
        f.ed->paste();
        CHECK(f.text() == "\nthreeone\ntwo");
        REQUIRE(f.ed->undo());  // the paste
        REQUIRE(f.ed->undo());  // "two"
        CHECK(f.text() == "two\nthree");
    }
    SUBCASE("appending after a copy from another document starts afresh") {
        Fixture f("kill_src1.txt", "abc\n");
        Fixture g("kill_src2.txt", "xyz\n");
        Editor other(*g.doc, f.clipboard);
        other.select_range(0, 3);
        REQUIRE(other.copy());
        f.at(0);
        f.ed->cut_to_line_end(true);
        CHECK(clip_text(f.clipboard) == "abc");
    }
}

TEST_CASE("OSC 52 carries the whole clipboard after an append") {
    Fixture f("kill_osc.txt", "ab\ncd");
    f.at(0);
    f.ed->cut_to_line_end(false);
    f.ed->cut_to_line_end(true);
    REQUIRE(f.terminal.size() == 2);
    CHECK(f.terminal[1] == "\x1b]52;c;YWIK\x1b\\");  // base64 of "ab\n"
}

TEST_CASE("typing over a selection is one undo node; Tab inserts one tab") {
    Fixture f("typing.txt", "abcdef");
    Editor& e = *f.ed;
    e.select_range(1, 4);
    e.insert_text("X");
    CHECK(f.text() == "aXef");
    REQUIRE(e.undo());
    CHECK(f.text() == "abcdef");
    f.at(0);
    e.insert_text("\t");
    CHECK(f.text() == "\tabcdef");
    e.newline();
    CHECK(f.text() == "\t\nabcdef");
}

TEST_CASE("OSC 52 is written for a copy of exactly 100 000 bytes and not for 100 001") {
    Fixture f("osc52.txt", std::string(100'001, 'q'));
    f.ed->select_range(0, 100'000);
    REQUIRE(f.ed->copy());
    REQUIRE(f.terminal.size() == 1);
    const std::string& seq = f.terminal[0];
    CHECK(seq.starts_with("\x1b]52;c;"));
    CHECK(seq.ends_with("\x1b\\"));
    CHECK(seq.size() == 7 + 133'336 + 2);  // base64 of 100 000 bytes
    CHECK(seq.substr(7, 8) == "cXFxcXFx");
    f.ed->select_range(0, 100'001);
    REQUIRE(f.ed->copy());
    CHECK(f.terminal.size() == 1);
    CHECK(f.clipboard.get()->length == 100'001);
}

TEST_CASE("after an in-place save, undo and an older copy still give the original bytes") {
    const std::string text = std::string(5000, 'A') + std::string(5000, 'B') + "\n";
    const fs::path p = scratch("inplace.txt");
    write_file(p, text);
    write_file(sidecar_path_for(p), "not a history file");  // no sidecar rebinding at all
    EventQueue q({});
    Clipboard clipboard;
    auto d = Document::open(p, q);
    REQUIRE(d);
    Document& doc = **d;
    CHECK(doc.history_state() == HistoryState::session_only);  // persistence is off
    CHECK(doc.history_unreadable());
    {
        Editor e(doc, clipboard);
        e.select_range(0, 4999);
        REQUIRE(e.copy());
        e.select_range(0, 5000);
        e.delete_backward();  // a 5000-byte Pieces payload into the file's mapping
        REQUIRE(doc.text().read(0, 1) == "B");
        REQUIRE(doc.save(SaveMode::in_place, &clipboard));
        CHECK(doc.text().read(0, doc.text().size()) == std::string(5000, 'B') + "\n");
        REQUIRE(e.undo());
        CHECK(doc.text().read(0, doc.text().size()) == text);
        e.select_range(10'001, 10'001);
        e.paste();
        CHECK(doc.text().read(10'001, 4999) == std::string(4999, 'A'));
    }
    std::ifstream in(p, std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == std::string(5000, 'B') + "\n");
}

TEST_CASE("indent: spaces to the next tab stop, or a tab character") {
    Fixture f("indent.txt", "ab\n");
    f.ed->select_range(0, 0);
    f.ed->indent(true);
    CHECK(f.text() == "    ab\n");
    CHECK(f.ed->cursor() == 4);
    f.ed->select_range(6, 6);  // after "ab": column 6
    f.ed->indent(true);
    CHECK(f.text() == "    ab  \n");  // two spaces reach the next stop, column 8
    f.ed->select_range(0, 0);
    f.ed->indent(false);
    CHECK(f.text() == "\t    ab  \n");
}

TEST_CASE("outdent: up to a tab width of leading spaces, or a leading tab, from each line touched") {
    Fixture f("outdent.txt", "      six\n  two\n\tone\nnone\n");
    f.ed->select_range(8, 8);  // on the first line
    f.ed->outdent();
    CHECK(f.text() == "  six\n  two\n\tone\nnone\n");
    CHECK(f.ed->cursor() == 4);  // moved left with its text
    f.ed->select_range(0, f.doc->text().size());  // every line
    f.ed->outdent();
    CHECK(f.text() == "six\ntwo\none\nnone\n");
    f.ed->outdent();  // nothing left to remove
    CHECK(f.text() == "six\ntwo\none\nnone\n");
}

TEST_CASE("two editors on one document, as two split views: an edit in one moves the other's cursor") {
    Fixture f("two_views.txt", "hello world\n");
    Clipboard clipboard;
    Editor other(*f.doc, clipboard);
    other.select_range(6, 6);  // before "world"
    f.ed->select_range(0, 0);
    f.ed->insert_text("say ");
    CHECK(f.text() == "say hello world\n");
    CHECK(other.cursor() == 10);  // still before "world"
    other.insert_text("big ");
    CHECK(f.text() == "say hello big world\n");
    CHECK(f.ed->cursor() == 4);  // the first editor's cursor was before the change: unmoved
}

TEST_CASE("Clipboard::set_text holds bytes of no document, and a paste copies them") {
    Fixture f("set_text.txt", "ab\n");
    f.clipboard.set_text("shown text");
    REQUIRE(f.clipboard.get() != nullptr);
    CHECK(f.clipboard.get()->source == 0);
    CHECK(f.clipboard.get()->length == 10);
    CHECK_FALSE(f.terminal.empty());  // OSC 52, as for a copy
    f.at(1);
    f.ed->paste();
    CHECK(f.text() == "ashown textb\n");
}

TEST_CASE("outdent keeps a backward selection over the same text") {
    Fixture f("outdent_back.txt", "  a\n  b\n");
    f.ed->select_range(8, 0);  // anchor at the end, cursor at the start
    f.ed->outdent();
    CHECK(f.text() == "a\nb\n");
    CHECK(f.ed->cursor() == 0);
    REQUIRE(f.ed->anchor().has_value());
    CHECK(*f.ed->anchor() == 4);
}

TEST_CASE("an in-place save leaves the text, and undo, independent of the bytes it overwrote") {
    Fixture f("in_place.txt", "abcdef\nsecond line\n");
    f.at(0);
    f.ed->insert_text("XY");
    REQUIRE(f.doc->save(SaveMode::in_place, &f.clipboard));
    CHECK(f.text() == "XYabcdef\nsecond line\n");
    std::ifstream in(f.doc->path(), std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "XYabcdef\nsecond line\n");
    REQUIRE(f.ed->undo());
    CHECK(f.text() == "abcdef\nsecond line\n");
    REQUIRE(f.ed->redo());
    CHECK(f.text() == "XYabcdef\nsecond line\n");
}

TEST_CASE("an edit that joins invalid bytes into a character leaves the cursor on its boundary") {
    Fixture f("join_bytes.txt", std::string("\x02\xe7\x0f\x9a\x9a\x9a", 6));
    f.ed->select_range(2, 4);  // the 0x0F between E7 and 9A 9A, and one 9A
    f.ed->cut();
    CHECK(f.text() == std::string("\x02\xe7\x9a\x9a", 4));  // E7 9A 9A is now one character
    CHECK(f.ed->cursor() == 1);  // at its lead byte, not inside it
}

TEST_CASE("text_columns: the display width of a label, wide characters two columns, marks none") {
    CHECK(text_columns("abc") == 3);
    CHECK(text_columns("\xc3\xa9t\xc3\xa9") == 3);         // été
    CHECK(text_columns("\xe6\xbc\xa2\xe5\xad\x97") == 4);  // 漢字: two wide characters
    CHECK(text_columns("e\xcc\x81") == 1);                 // e and a combining accent
    CHECK(text_columns("\xff" "a") == 2);                  // an invalid byte is one column
    CHECK(text_columns("") == 0);
}

TEST_CASE("an in-place save over a file that cannot be written fails and leaves it as it was") {
    if (::geteuid() == 0) return;  // root writes regardless of the permission bits
    Fixture f("in_place_locked.txt", "abc\n");
    fs::permissions(f.doc->path(), fs::perms::owner_read);
    f.ed->insert_text("X");
    const Status s = f.doc->save(SaveMode::in_place, &f.clipboard);
    fs::permissions(f.doc->path(), fs::perms::owner_read | fs::perms::owner_write);
    REQUIRE_FALSE(s);
    CHECK(s.error().message.starts_with("open "));
    std::ifstream in(f.doc->path(), std::ios::binary);
    CHECK(std::string(std::istreambuf_iterator<char>(in), {}) == "abc\n");
    CHECK(f.doc->is_dirty());
}

TEST_CASE("a paste that arrives in several pieces is undone in one step; two pastes are two") {
    Fixture f("paste_pieces.txt", "start\n");
    f.at(0);
    f.ed->paste_text("one ", true);
    f.ed->paste_text("two ", true);
    f.ed->paste_text("three ", false);
    f.ed->paste_text("again ");
    CHECK(f.text() == "one two three again start\n");
    REQUIRE(f.ed->undo());
    CHECK(f.text() == "one two three start\n");
    REQUIRE(f.ed->undo());
    CHECK(f.text() == "start\n");
}
