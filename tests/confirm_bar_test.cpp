#include <doctest/doctest.h>

#include <optional>
#include <string>

#include "platform/terminal.hpp"
#include "ui/confirm_bar.hpp"
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
KeyEvent ch(char32_t c, std::uint8_t mods = 0) { return KeyEvent{Key::Char, c, mods}; }

std::string screen_row(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) out.append(s.cell(row, c).utf8.data(), s.cell(row, c).len);
    while (!out.empty() && out.back() == ' ') out.pop_back();
    return out;
}

struct Fixture {
    ConfirmBar bar;
    std::optional<int> chosen;
    void ask() {
        bar.open("Save changes to notes.md before closing?", {"Save", "Discard", "Cancel"}, 2, [this](int c) { chosen = c; });
    }
};

}  // namespace

TEST_CASE("Tab, Right, Shift+Tab and Left move the focus, wrapping; Enter chooses it") {
    Fixture f;
    f.ask();
    CHECK(f.bar.is_open());
    CHECK(f.bar.focused() == 0);
    f.bar.handle_key(key(Key::Tab));
    f.bar.handle_key(key(Key::Right));
    CHECK(f.bar.focused() == 2);
    f.bar.handle_key(key(Key::Tab));
    CHECK(f.bar.focused() == 0);  // wraps
    f.bar.handle_key(key(Key::BackTab));
    CHECK(f.bar.focused() == 2);
    f.bar.handle_key(key(Key::Left));
    CHECK(f.bar.focused() == 1);
    f.bar.handle_key(key(Key::Enter));
    CHECK(f.chosen == 1);
    CHECK_FALSE(f.bar.is_open());
}

TEST_CASE("a first letter chooses its button; not with Ctrl or Alt; Esc chooses the Esc choice") {
    Fixture f;
    f.ask();
    f.bar.handle_key(ch(U'x'));  // no such button: consumed
    CHECK(f.bar.is_open());
    f.bar.handle_key(ch(U'd', kAlt));
    CHECK(f.bar.is_open());
    f.bar.handle_key(ch(U'D'));
    CHECK(f.chosen == 1);
    f.ask();
    f.bar.handle_key(key(Key::Escape));
    CHECK(f.chosen == 2);
}

TEST_CASE("the bar is closed when its callback runs, so the callback can ask again") {
    ConfirmBar bar;
    int asked = 0;
    bar.open("First?", {"Yes", "No"}, 1, [&](int) {
        CHECK_FALSE(bar.is_open());
        bar.open("Second?", {"Yes", "No"}, 1, [&](int) { ++asked; });
    });
    bar.handle_key(key(Key::Enter));
    CHECK(bar.is_open());
    bar.handle_key(key(Key::Enter));
    CHECK(asked == 1);
}

TEST_CASE("rows: the rule, the question's lines and the buttons; nothing when closed") {
    Fixture f;
    CHECK(f.bar.rows(80) == 0);
    f.ask();
    CHECK(f.bar.rows(80) == 3);
    CHECK(f.bar.rows(24) > 3);  // the question wraps
}

TEST_CASE("render: rule, question, buttons with the focused one marked and the first letters underlined") {
    Fixture f;
    f.ask();
    NullTerminal term;
    Screen screen(term);
    screen.resize({6, 60});
    f.bar.render(screen, Rect{2, 0, 3, 60});
    CHECK(screen_row(screen, 2).starts_with("──"));
    CHECK(screen_row(screen, 3) == " Save changes to notes.md before closing?");
    const std::string buttons = screen_row(screen, 4);
    CHECK(buttons == " [>Save<]  [Discard]  [Cancel]");
    const int save = static_cast<int>(buttons.find("Save"));
    CHECK((screen.cell(4, save).attr.flags & kUnderline) != 0);  // its key letter
    CHECK(screen.cell(4, save + 1).attr == attr_for(Style::menu_selected));  // the focused button
    const int discard = static_cast<int>(buttons.find("Discard"));
    CHECK((screen.cell(4, discard).attr.flags & kUnderline) != 0);
    CHECK_FALSE(screen.cell(4, discard + 1).attr == attr_for(Style::menu_selected));
    CHECK_FALSE(screen.cursor_visible());
}
