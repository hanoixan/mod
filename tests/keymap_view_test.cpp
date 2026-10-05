#include <doctest/doctest.h>

#include <string>
#include <string_view>
#include <vector>

#include "app/commands.hpp"
#include "app/keymap.hpp"
#include "platform/terminal.hpp"
#include "ui/keymap_view.hpp"
#include "ui/screen.hpp"
#include "ui/theme.hpp"

using namespace mod;

namespace {

using C = CommandId;

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
KeyEvent ch(char32_t c) { return KeyEvent{Key::Char, c, 0}; }
KeyEvent ctrl(char c) { return KeyEvent{Key::CtrlLetter, static_cast<char32_t>(c), kCtrl}; }

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        out.append(cell.utf8.data(), cell.len);
    }
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

struct Fixture {
    Keymap keymap;
    KeymapView view;
    Fixture() { view.open(keymap); }
    void type(std::string_view text) {
        for (const char c : text) view.handle_key(ch(static_cast<char32_t>(c)));
    }
    std::vector<std::string> names() const {
        std::vector<std::string> out;
        for (std::size_t i = 0; i < view.row_count(); ++i) {
            const std::string row = view.row_text(i);
            out.push_back(row.substr(0, row.find("  ")));
        }
        return out;
    }
};

std::size_t bindable_commands() {
    std::size_t n = 0;
    for (const CommandInfo& info : all_commands()) n += info.bindable ? 1 : 0;
    return n;
}

}  // namespace

TEST_CASE("the editor lists every bindable command with its keys") {
    Fixture f;
    CHECK(f.view.row_count() == bindable_commands());
    CHECK(f.view.selected() == 0);
    bool save = false, save_as = false, line_start = false;
    for (std::size_t i = 0; i < f.view.row_count(); ++i) {
        const std::string row = f.view.row_text(i);
        CHECK(row.find("Recent Setting") == std::string::npos);
        if (row.starts_with("Save  ")) save = row.ends_with("Ctrl+S");
        if (row.starts_with("Save As  ")) save_as = row.ends_with("(none)");
        if (row.starts_with("Move Line Start  ")) line_start = row.ends_with("Ctrl+A, Home");
    }
    CHECK(save);
    CHECK(save_as);
    CHECK(line_start);
}

TEST_CASE("typing filters the list by command name and by key, in any case") {
    Fixture f;
    f.type("go to");
    CHECK(f.view.search() == "go to");
    CHECK(f.names() == std::vector<std::string>{"Go to Line"});
    CHECK(f.view.selected_command() == C::GotoLine);
    for (int i = 0; i < 5; ++i) f.view.handle_key(key(Key::Backspace));
    CHECK(f.view.search().empty());
    CHECK(f.view.row_count() == bindable_commands());
    f.type("CTRL+Q");
    CHECK(f.names() == std::vector<std::string>{"Exit"});
    f.view.handle_key(key(Key::Backspace, kCtrl));  // clears the field
    f.type("word left");                             // every word must match
    CHECK(f.names() == std::vector<std::string>{"Move Word Left", "Select Word Left"});
    f.view.handle_key(key(Key::Backspace, kCtrl));
    f.type("gotoline");  // the internal name matches too
    CHECK(f.names() == std::vector<std::string>{"Go to Line"});
    f.view.handle_key(key(Key::Backspace, kCtrl));
    f.type("zzz");
    CHECK(f.view.row_count() == 0);
    CHECK_FALSE(f.view.selected_command().has_value());
    CHECK(f.view.handle_key(key(Key::Enter)).message == "no command selected");
    CHECK_FALSE(f.view.capturing());
}

TEST_CASE("pasted text goes into the search field, up to its first line break") {
    Fixture f;
    f.view.handle_paste("undo\rredo");
    CHECK(f.view.search() == "undo");
    CHECK(f.names() == std::vector<std::string>{"Undo", "Undo History"});
}

TEST_CASE("Up, Down, Home, End and the page keys move the selection") {
    Fixture f;
    const std::size_t last = f.view.row_count() - 1;
    f.view.handle_key(key(Key::Up));
    CHECK(f.view.selected() == 0);
    f.view.handle_key(key(Key::Down));
    CHECK(f.view.selected() == 1);
    f.view.handle_key(key(Key::End));
    CHECK(f.view.selected() == last);
    f.view.handle_key(key(Key::Down));
    CHECK(f.view.selected() == last);
    f.view.handle_key(key(Key::PageUp));
    CHECK(f.view.selected() < last);
    f.view.handle_key(key(Key::Home));
    CHECK(f.view.selected() == 0);
    f.type("find");  // the selection returns to the top when the filter changes
    f.view.handle_key(key(Key::Down));
    f.type("x");
    CHECK(f.view.selected() == 0);
}

TEST_CASE("Enter captures the next key and adds it to the selected command") {
    Fixture f;
    f.type("go to");
    KeymapKeyResult r = f.view.handle_key(key(Key::Enter));
    CHECK(f.view.capturing());
    CHECK_FALSE(r.changed);
    r = f.view.handle_key(key(Key::F5));
    CHECK_FALSE(f.view.capturing());
    CHECK(r.changed);
    CHECK(r.message == "F5 added to Go to Line");
    CHECK(f.keymap.lookup(key(Key::F5)) == C::GotoLine);
    CHECK(f.keymap.lookup(ctrl('g')) == C::GotoLine);
    CHECK(f.view.row_text(0).ends_with("Ctrl+G, F5 *"));  // the star marks a change from the defaults
    CHECK(f.view.search() == "go to");                    // a captured key never reaches the search field
}

TEST_CASE("a captured key that another command has is refused with the owner's name") {
    Fixture f;
    f.type("go to");
    f.view.handle_key(key(Key::Enter));
    KeymapKeyResult r = f.view.handle_key(ctrl('s'));
    CHECK_FALSE(r.changed);
    CHECK(r.message == "Ctrl+S is bound to Save; remove it there first");
    CHECK_FALSE(f.view.capturing());
    CHECK(f.keymap.lookup(ctrl('s')) == C::Save);
    CHECK(f.keymap.is_default(C::GotoLine));

    f.view.handle_key(key(Key::Enter));
    r = f.view.handle_key(ctrl('g'));
    CHECK_FALSE(r.changed);
    CHECK(r.message == "Ctrl+G is already bound to Go to Line");

    f.view.handle_key(key(Key::Enter));
    r = f.view.handle_key(ch(U'x'));
    CHECK_FALSE(r.changed);
    CHECK(r.message == "X types text; it cannot be bound");
    CHECK(f.view.search() == "go to");
}

TEST_CASE("Esc cancels a capture without closing; Esc then closes") {
    Fixture f;
    f.view.handle_key(key(Key::Enter));
    REQUIRE(f.view.capturing());
    KeymapKeyResult r = f.view.handle_key(key(Key::Escape));
    CHECK_FALSE(r.closed);
    CHECK_FALSE(f.view.capturing());
    CHECK(f.view.is_open());
    r = f.view.handle_key(key(Key::Escape));
    CHECK(r.closed);
    CHECK_FALSE(f.view.is_open());
}

TEST_CASE("Delete removes the command's last key; with none left it says so") {
    Fixture f;
    f.type("move line start");
    KeymapKeyResult r = f.view.handle_key(key(Key::Delete));
    CHECK(r.changed);
    CHECK(r.message == "Home removed from Move Line Start");
    CHECK(f.view.row_text(0).ends_with("Ctrl+A *"));
    r = f.view.handle_key(key(Key::Delete));
    CHECK(r.changed);
    CHECK(f.view.row_text(0).ends_with("(none) *"));
    r = f.view.handle_key(key(Key::Delete));
    CHECK_FALSE(r.changed);
    CHECK(r.message == "Move Line Start has no key to remove");
}

TEST_CASE("Ctrl+R resets the selected command; Alt+R asks to reset everything") {
    Fixture f;
    f.type("go to");
    f.view.handle_key(key(Key::Enter));
    f.view.handle_key(key(Key::F5));
    KeymapKeyResult r = f.view.handle_key(ctrl('r'));
    CHECK(r.changed);
    CHECK(r.message == "Go to Line reset to its default keys");
    CHECK(f.keymap.is_default(C::GotoLine));
    r = f.view.handle_key(ctrl('r'));
    CHECK_FALSE(r.changed);  // already the defaults

    f.view.handle_key(key(Key::Enter));
    f.view.handle_key(key(Key::F5));
    r = f.view.handle_key(KeyEvent{Key::Char, U'r', kAlt});
    CHECK(r.reset_all);
    CHECK_FALSE(r.changed);  // nothing happens until the caller has asked
    CHECK_FALSE(f.keymap.is_default(C::GotoLine));
    f.view.reset_all();
    CHECK(f.keymap.is_default(C::GotoLine));
    CHECK(f.keymap.overrides().size() == 0);
}

TEST_CASE("a reset says which default keys stay with another command") {
    Fixture f;
    REQUIRE(f.keymap.unbind(C::MoveLineStart, ctrl('a')));
    REQUIRE(f.keymap.bind(C::Find, ctrl('a')).kind == BindOutcome::bound);
    f.type("move line start");
    const KeymapKeyResult r = f.view.handle_key(ctrl('r'));
    CHECK(r.message == "Move Line Start reset; Ctrl+A stays with Find/Replace");
}

TEST_CASE("render: the search field, the rows, the selected row highlighted, and the capture line") {
    Fixture f;
    NullTerminal term;
    Screen screen(term);
    screen.resize({12, 60});
    const Rect area{1, 1, 10, 58};
    f.type("find");
    f.view.render(screen, area);
    CHECK(screen_row(screen, 1).find("Search: find") != std::string::npos);
    CHECK(screen_row(screen, 2).find("Find/Replace") != std::string::npos);
    CHECK(screen_row(screen, 2).find("Ctrl+F") != std::string::npos);
    CHECK(screen.cell(2, 1).attr == attr_for(Style::list_selected));
    CHECK(screen.cell(3, 1).attr != attr_for(Style::list_selected));
    CHECK(screen_row(screen, 3).find("Find Next") != std::string::npos);
    CHECK(screen_row(screen, 0).empty());  // nothing outside the area

    f.view.handle_key(key(Key::Enter));
    f.view.render(screen, area);
    CHECK(screen_row(screen, 1).find("Press the new key for Find/Replace") != std::string::npos);
    CHECK(screen_row(screen, 1).find("Esc cancels") != std::string::npos);
}

TEST_CASE("render: a long list scrolls to keep the selected row visible") {
    Fixture f;
    NullTerminal term;
    Screen screen(term);
    screen.resize({8, 60});
    const Rect area{1, 0, 5, 60};  // the search row and four list rows
    f.view.handle_key(key(Key::End));
    f.view.render(screen, area);
    const std::string last = f.view.row_text(f.view.row_count() - 1);
    CHECK(screen_row(screen, 5).find(last.substr(0, last.find("  "))) != std::string::npos);
    f.view.handle_key(key(Key::Home));
    f.view.render(screen, area);
    const std::string first = f.view.row_text(0);
    CHECK(screen_row(screen, 2).find(first.substr(0, first.find("  "))) != std::string::npos);
}
