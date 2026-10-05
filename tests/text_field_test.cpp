#include <doctest/doctest.h>

#include <span>
#include <string>

#include "platform/terminal.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

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
KeyEvent ch(char32_t c) { return KeyEvent{Key::Char, c, 0}; }

std::string row_text(const Screen& s, int row) {
    std::string out;
    for (int c = 0; c < s.cols(); ++c) {
        const Cell& cell = s.cell(row, c);
        if (cell.width == 0) continue;
        out.append(cell.utf8.data(), cell.len);
    }
    return out;
}

}  // namespace

#include "ui/text_field.hpp"

TEST_CASE("typing inserts at the cursor and the cursor moves") {
    TextField f("ac");
    CHECK(f.handle_key(key(Key::Left)));
    CHECK(f.handle_key(ch(U'b')));
    CHECK(f.text() == "abc");
    CHECK(f.handle_key(key(Key::Home)));
    CHECK(f.at_start());
    CHECK(f.handle_key(key(Key::End)));
    CHECK(f.at_end());
}

TEST_CASE("Backspace and Delete remove whole clusters") {
    TextField f("e\xCC\x81x\xF0\x9F\x87\xAB\xF0\x9F\x87\xB7");  // e + acute, x, a flag
    CHECK(f.handle_key(key(Key::Backspace)));
    CHECK(f.text() == "e\xCC\x81x");
    f.handle_key(key(Key::Left));
    f.handle_key(key(Key::Left));
    CHECK(f.at_start());
    CHECK(f.handle_key(key(Key::Delete)));
    CHECK(f.text() == "x");
}

TEST_CASE("Left at the start and Right at the end are left to the dialog") {
    TextField f("ab");
    f.handle_key(key(Key::Home));
    CHECK_FALSE(f.handle_key(key(Key::Left)));
    f.handle_key(key(Key::End));
    CHECK_FALSE(f.handle_key(key(Key::Right)));
    CHECK(f.text() == "ab");
}

TEST_CASE("keys for the dialog return false") {
    TextField f("ab");
    for (const Key k : {Key::Tab, Key::BackTab, Key::Enter, Key::Escape, Key::Up, Key::Down, Key::PageUp, Key::PageDown})
        CHECK_FALSE(f.handle_key(key(k)));
    CHECK_FALSE(f.handle_key(KeyEvent{Key::CtrlLetter, U'a', kCtrl}));
    CHECK(f.text() == "ab");
}

TEST_CASE("insert drops line breaks and set_text puts the cursor at the end") {
    TextField f;
    f.insert("one\r\ntwo\nthree");
    CHECK(f.text() == "onetwothree");
    f.set_text("x");
    CHECK(f.at_end());
}

TEST_CASE("render scrolls to keep the cursor visible, pads, and shows the cursor only when focused") {
    NullTerminal term;
    Screen screen{term};
    screen.resize({3, 20});
    TextField f("abcdefghijklmnop");
    f.render(screen, 1, 2, 6, Attr{}, true);
    CHECK(screen.cursor_visible());
    CHECK(screen.cursor_col() <= 7);
    CHECK(row_text(screen, 1).substr(2, 6) == "lmnop ");
    f.handle_key(key(Key::Home));
    f.render(screen, 1, 2, 6, Attr{}, false);
    CHECK(row_text(screen, 1).substr(2, 6) == "abcdef");
}
