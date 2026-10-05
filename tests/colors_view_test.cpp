#include <doctest/doctest.h>

#include <string>

#include "platform/terminal.hpp"
#include "ui/colors_view.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"

using namespace mod;

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

KeyEvent key(Key k, std::uint8_t mods = 0) { return KeyEvent{k, 0, mods}; }

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

// The index of the row for `name`, or row_count() when there is none.
std::size_t row_of(const ColorsView& v, std::string_view name) {
    for (std::size_t i = 0; i < v.row_count(); ++i)
        if (v.row_text(i).starts_with(std::string(name) + " ")) return i;
    return v.row_count();
}

TEST_CASE("every color name has one row under its group; headings are never selected") {
    ColorTheme t;
    ColorsView v;
    v.open(t);
    CHECK(v.is_open());
    CHECK(v.row_count() == color_names().size() + 4);
    CHECK(v.row_text(0) == "Text");
    CHECK(v.selected() == 1);  // the first entry, not the heading
    for (const ColorEntry& e : color_names()) {
        CAPTURE(e.name);
        CHECK(row_of(v, e.name) < v.row_count());
    }
    v.handle_key(key(Key::End));
    CHECK(v.selected_name() == color_names().back().name);
    v.handle_key(key(Key::Home));
    CHECK(v.selected_name() == color_names()[0].name);
    // Down past the end of a group skips the next heading.
    const std::size_t markdown = row_of(v, "markdownHeading1");
    REQUIRE(v.row_text(markdown - 1) == "Markdown");
    while (v.selected() + 2 < markdown) v.handle_key(key(Key::Down));
    v.handle_key(key(Key::Down));
    CHECK(v.selected() == markdown);
    v.handle_key(key(Key::Up));
    CHECK(v.selected() == markdown - 2);
    v.handle_key(key(Key::PageDown));
    CHECK(v.row_text(v.selected()) != "Markdown");
    CHECK(v.row_text(v.selected()) != "Modifiers");
}

TEST_CASE("a changed entry shows its spec and a star") {
    ColorTheme t;
    REQUIRE(t.set("keyword", "bold red"));
    ColorsView v;
    v.open(t);
    CHECK(v.row_text(row_of(v, "keyword")).ends_with("bold red *"));
    CHECK(v.row_text(row_of(v, "comment")).ends_with("dim italic"));
    CHECK(v.row_text(row_of(v, "comment")).find("Sample") != std::string::npos);
}

TEST_CASE("Enter asks to edit, Delete and Ctrl+R to reset, Alt+R to reset all, Esc closes") {
    ColorTheme t;
    ColorsView v;
    v.open(t);
    const std::string first(color_names()[0].name);
    CHECK(v.handle_key(key(Key::Enter)).edit == first);
    CHECK(v.handle_key(key(Key::Delete)).reset == first);
    CHECK(v.handle_key(KeyEvent{Key::CtrlLetter, U'r', kCtrl}).reset == first);
    CHECK(v.handle_key(KeyEvent{Key::Char, U'r', kAlt}).reset_all);
    CHECK_FALSE(v.handle_key(KeyEvent{Key::Char, U'r', 0}).reset_all);
    CHECK(v.is_open());
    CHECK(v.handle_key(key(Key::Escape)).closed);
    CHECK_FALSE(v.is_open());
}

TEST_CASE("render: each sample in its look, a modifier's on a variable, nothing outside the area") {
    ColorTheme t;
    REQUIRE(t.set("string", "bright-red on-blue"));
    ColorsView v;
    v.open(t);
    NullTerminal term;
    Screen screen(term);
    screen.resize({70, 80});
    v.render(screen, Rect{1, 0, 68, 80});
    CHECK(screen_row(screen, 0).empty());
    int checked = 0;
    for (int r = 1; r < 69; ++r) {
        const std::string row = screen_row(screen, r);
        const auto sample = static_cast<int>(row.find("Sample"));
        if (row.find(" string ") == 1) {
            CHECK(screen.cell(r, sample).attr == t.attr(Style::lsp_string));
            ++checked;
        }
        if (row.find(" deprecated ") == 1) {
            CHECK(screen.cell(r, sample).attr == t.attr(Style::lsp_variable, kModDeprecated));
            ++checked;
        }
    }
    CHECK(checked == 2);
}

TEST_CASE("render: a short area scrolls to keep the selection visible") {
    ColorTheme t;
    ColorsView v;
    v.open(t);
    NullTerminal term;
    Screen screen(term);
    screen.resize({10, 80});
    const Rect area{0, 0, 8, 80};
    v.handle_key(key(Key::End));
    v.render(screen, area);
    bool seen = false;
    for (int r = 0; r < 8; ++r) seen = seen || screen_row(screen, r).find(color_names().back().name) != std::string::npos;
    CHECK(seen);
}

}  // namespace
