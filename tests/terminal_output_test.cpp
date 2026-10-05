#include <doctest/doctest.h>

#include <map>
#include <string>
#include <vector>

#include "platform/terminal.hpp"
#include "platform/terminal_output.hpp"
#include "text/utf8.hpp"
#include "ui/screen.hpp"

using namespace mod;

namespace {

// A terminal that keeps what is written to it.
class RecordingTerminal : public Terminal {
public:
    std::string written;
    Status enter_raw_mode() override { return {}; }
    void restore() noexcept override {}
    TerminalSize size() override { return {}; }
    Result<std::size_t> read_input(std::span<std::byte>) override { return 0; }
    Status write(std::span<const std::byte> bytes) override {
        written.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return {};
    }
    WaitEvents wait(int) override { return timed_out; }
    void wake() noexcept override {}
};

std::optional<TerminalMode> from_env(std::map<std::string, std::string> vars) {
    return mode_from_environment([&](const char* name) -> const char* {
        const auto it = vars.find(name);
        return it == vars.end() ? nullptr : it->second.c_str();
    });
}

std::string sgr(const TerminalOutput& o, Attr a) {
    std::string out;
    o.append_attr(out, a);
    return out;
}

}  // namespace

TEST_CASE("xterm: the sequences of earlier versions, byte for byte") {
    const TerminalOutput& x = output_for(TerminalMode::xterm);
    CHECK(x.mode() == TerminalMode::xterm);
    CHECK(x.enter() == "\x1b[22;0t\x1b[?1049h\x1b[?2004h\x1b[?25l");  // the title saved first
    CHECK(x.leave() == "\x1b[?2004l\x1b[?1049l\x1b[?25h\x1b[0m\x1b[0 q\x1b[23;0t");  // the terminal's own cursor shape and title back
    CHECK(x.begin_frame() == "\x1b[?2026h\x1b[?25l\x1b[?7l");
    CHECK(x.end_frame() == "\x1b[0m\x1b[?7h");
    CHECK(x.show_cursor() == "\x1b[?25h");
    CHECK(x.end_sync() == "\x1b[?2026l");
    CHECK(sgr(x, Attr{}) == "\x1b[0m");
    CHECK(sgr(x, Attr{1, 4, kBold | kDim | kItalic | kUnderline | kReverse | kStrike}) == "\x1b[0;1;2;3;4;7;9;31;44m");
    CHECK(sgr(x, Attr{9, 12, 0}) == "\x1b[0;91;104m");
}

TEST_CASE("vt100: no color, no dim, italic or strike, and no private modes beyond autowrap") {
    const TerminalOutput& v = output_for(TerminalMode::vt100);
    CHECK(v.mode() == TerminalMode::vt100);
    CHECK(v.enter().empty());
    CHECK(v.leave() == "\x1b[0m\x1b[2J\x1b[H");
    CHECK(v.show_cursor().empty());
    CHECK(v.end_sync().empty());
    for (std::string_view s : {v.begin_frame(), v.end_frame()}) {
        CHECK(s.find("?2026") == std::string_view::npos);
        CHECK(s.find("?25") == std::string_view::npos);
        CHECK(s.find("?1049") == std::string_view::npos);
        CHECK(s.find("?2004") == std::string_view::npos);
    }
    CHECK(sgr(v, Attr{1, 4, kBold | kDim | kItalic | kUnderline | kReverse | kStrike}) == "\x1b[0;1;4;7m");
    CHECK(sgr(v, Attr{9, 12, kDim | kItalic}) == "\x1b[0m");
}

TEST_CASE("a Screen flushed through each output writes only that output's sequences") {
    for (TerminalMode m : {TerminalMode::xterm, TerminalMode::vt100}) {
        CAPTURE(static_cast<int>(m));
        RecordingTerminal t;
        Screen screen(t);
        screen.set_output(output_for(m));
        screen.resize({3, 10});
        screen.print(0, 0, 10, "hi", Attr{1, kDefaultColor, kBold | kItalic});
        screen.set_cursor(1, 1, true);
        REQUIRE(screen.flush());
        const bool xterm = m == TerminalMode::xterm;
        CHECK((t.written.find("\x1b[?2026h") != std::string::npos) == xterm);
        CHECK((t.written.find("\x1b[?25h") != std::string::npos) == xterm);
        CHECK((t.written.find(";31") != std::string::npos) == xterm);
        CHECK((t.written.find(";3") != std::string::npos) == xterm);  // italic, or the color
        CHECK(t.written.find("\x1b[0;1") != std::string::npos);       // bold either way
        CHECK(t.written.find("hi") != std::string::npos);
        CHECK(t.written.find("\x1b[2;2H") != std::string::npos);      // the cursor placed
    }
}

TEST_CASE("detection from the environment") {
    for (const char* term : {"vt100", "vt102", "vt220", "vt52", "dumb"}) {
        CAPTURE(term);
        CHECK(from_env({{"TERM", term}, {"COLORTERM", "truecolor"}}) == TerminalMode::vt100);  // the vt name wins
    }
    for (const char* term : {"xterm", "xterm-256color", "screen-256color", "tmux-256color", "rxvt-unicode", "alacritty",
                             "xterm-kitty", "foot", "wezterm", "konsole", "gnome-256color", "st-256color", "putty", "linux"}) {
        CAPTURE(term);
        CHECK(from_env({{"TERM", term}}) == TerminalMode::xterm);
    }
    CHECK(from_env({{"COLORTERM", "truecolor"}}) == TerminalMode::xterm);
    CHECK(from_env({{"TERM_PROGRAM", "Apple_Terminal"}}) == TerminalMode::xterm);
    CHECK(from_env({{"WT_SESSION", "x"}}) == TerminalMode::xterm);
    CHECK_FALSE(from_env({}).has_value());
    CHECK_FALSE(from_env({{"TERM", "ansi"}}).has_value());  // unknown: ask the terminal
}

TEST_CASE("device attribute replies") {
    CHECK(mode_from_device_attributes("\x1b[?1;2c") == TerminalMode::vt100);
    CHECK(mode_from_device_attributes("\x1b[?1;0c") == TerminalMode::vt100);
    CHECK(mode_from_device_attributes("\x1b[?6c") == TerminalMode::vt100);
    CHECK(mode_from_device_attributes("\x1b[?62;1;6;22c") == TerminalMode::xterm);
    CHECK(mode_from_device_attributes("\x1b[?65;1;9c") == TerminalMode::xterm);
    CHECK_FALSE(mode_from_device_attributes("\x1b[?62;1").has_value());  // not yet complete
    CHECK_FALSE(mode_from_device_attributes("abc").has_value());
    CHECK_FALSE(mode_from_device_attributes("\x1b[?x;c").has_value());
    // A reply among typed bytes is found, so the rest can be kept as input.
    const std::string bytes = std::string("ab") + "\x1b[?64;1c" + "cd";
    const auto range = find_device_attributes(bytes);
    REQUIRE(range.has_value());
    CHECK(range->first == 2);
    CHECK(range->second == 2 + std::string("\x1b[?64;1c").size());
    CHECK(mode_from_device_attributes(bytes) == TerminalMode::xterm);
}

TEST_CASE("cursor shapes: xterm sends the DECSCUSR code for each style, vt100 nothing") {
    const TerminalOutput& x = output_for(TerminalMode::xterm);
    CHECK(x.cursor_shape(CursorStyle::bar) == "\x1b[6 q");
    CHECK(x.cursor_shape(CursorStyle::bar_blink) == "\x1b[5 q");
    CHECK(x.cursor_shape(CursorStyle::block) == "\x1b[2 q");
    CHECK(x.cursor_shape(CursorStyle::block_blink) == "\x1b[1 q");
    CHECK(x.cursor_shape(CursorStyle::underline) == "\x1b[4 q");
    CHECK(x.cursor_shape(CursorStyle::underline_blink) == "\x1b[3 q");
    CHECK(output_for(TerminalMode::vt100).cursor_shape(CursorStyle::block).empty());
}

TEST_CASE("a Screen sends the cursor shape once, again after a change or a full redraw") {
    RecordingTerminal t;
    Screen screen(t);
    screen.resize({3, 10});
    screen.set_cursor(0, 0, true);
    REQUIRE(screen.flush());
    CHECK(t.written.find("\x1b[6 q") != std::string::npos);  // a bar by default
    t.written.clear();
    REQUIRE(screen.flush());
    CHECK(t.written.find(" q") == std::string::npos);  // not sent again
    screen.set_cursor_style(CursorStyle::block);
    REQUIRE(screen.flush());
    CHECK(t.written.find("\x1b[2 q") != std::string::npos);
    t.written.clear();
    screen.invalidate();
    REQUIRE(screen.flush());
    CHECK(t.written.find("\x1b[2 q") != std::string::npos);
}

namespace {

bool valid_utf8(std::string_view s) {
    for (std::size_t i = 0; i < s.size();) {
        const Decoded d = decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
        if (!d.valid) return false;
        i += d.len;
    }
    return true;
}

}  // namespace

TEST_CASE("no text drawn on the screen can reach the terminal as a control sequence") {
    RecordingTerminal term;
    Screen screen(term);
    screen.resize({4, 40});
    // A file name or message with an OSC title change, a C1 CSI (U+009B), DEL and a bell.
    screen.print(0, 0, 40, "a\x1b]0;pwned\x07z", Attr{});
    screen.print(1, 0, 40, "b\xc2\x9b" "2J\xc2\x9d" "0;x\x07y\x7f", Attr{});
    // Straight into a cell, as a text field does with a pasted or typed character.
    screen.put(2, 0, "\x1b", 2, Attr{});
    screen.put(2, 2, "\xc2\x9b", 6, Attr{});
    screen.put(2, 3, "c", 1, Attr{});
    // Raw 8-bit C1 bytes (an 8-bit CSI and OSC), not UTF-8 at all, as a pasted byte would be.
    screen.put(2, 5, "\x9b", 1, Attr{});
    screen.put(2, 6, "\x9d", 1, Attr{});
    screen.print(3, 0, 40, "d\x9b" "2J\x9d" "0;x\x9c", Attr{});
    for (int r = 0; r < 4; ++r) {
        for (int c = 0; c < 40; ++c) {
            const Cell& cell = screen.cell(r, c);
            const std::string_view bytes(cell.utf8.data(), cell.len);
            CAPTURE(r);
            CAPTURE(c);
            for (unsigned char b : bytes) CHECK((b >= 0x20 && b != 0x7f));
            CHECK(bytes.find("\xc2\x80") == std::string_view::npos);
            for (unsigned char second = 0x80; second <= 0x9f; ++second) {
                const char c1[] = {'\xc2', static_cast<char>(second), 0};
                CHECK(bytes.find(c1) == std::string_view::npos);
            }
            CHECK(valid_utf8(bytes));
            CHECK(cell.width <= 2);
        }
    }
    CHECK(screen.cell(2, 3).utf8[0] == 'c');  // the grid is not thrown off by a bogus width
    REQUIRE(screen.flush());
    CHECK(term.written.find("\x1b]") == std::string::npos);
    CHECK(term.written.find('\x07') == std::string::npos);
    CHECK(term.written.find("\xc2\x9b") == std::string::npos);
    CHECK(term.written.find("\xc2\x9d") == std::string::npos);
    CHECK(term.written.find('\x9b') == std::string::npos);
    CHECK(term.written.find('\x9d') == std::string::npos);
}

TEST_CASE("a wide character put over the second half of another, in the last column, leaves no half glyph") {
    RecordingTerminal term;
    Screen screen(term);
    screen.resize({1, 4});
    screen.put(0, 2, "\xe3\x81\x82", 2, Attr{});  // あ in columns 2-3
    screen.put(0, 3, "\xe3\x81\x84", 2, Attr{});  // い where only one column is left
    CHECK(screen.cell(0, 2).width == 1);         // あ is gone, not left claiming two columns
    CHECK(screen.cell(0, 2).utf8[0] == ' ');
    CHECK(screen.cell(0, 3).width == 1);
}

TEST_CASE("the title: OSC 0 in xterm mode, nothing on a VT100, and no control can get through") {
    std::string out;
    output_for(TerminalMode::xterm).append_title(out, "mod:notes.md *");
    CHECK(out == "\x1b]0;mod:notes.md *\a");
    out.clear();
    output_for(TerminalMode::xterm).append_title(out, "mod:\x07" "evil\x1b]0;x\x9c\xc2\x9b\xff\x7f.txt");
    CHECK(out == "\x1b]0;mod:?evil?]0;x????.txt\a");
    out.clear();
    output_for(TerminalMode::xterm).append_title(out, "mod:\xc3\xa9t\xc3\xa9.md");  // UTF-8 stays
    CHECK(out == "\x1b]0;mod:\xc3\xa9t\xc3\xa9.md\a");
    out.clear();
    output_for(TerminalMode::vt100).append_title(out, "mod:notes.md");
    CHECK(out.empty());
}

TEST_CASE("add_flags dims an area already drawn, leaving its text and colors") {
    RecordingTerminal term;
    Screen screen(term);
    screen.resize({4, 10});
    screen.print(1, 0, 10, "abcdefghij", Attr{3, 4, kBold});
    screen.add_flags(Rect{1, 2, 1, 3}, kDim);
    CHECK(screen.cell(1, 1).attr == Attr{3, 4, kBold});
    for (int c = 2; c < 5; ++c) CHECK(screen.cell(1, c).attr == Attr{3, 4, static_cast<std::uint8_t>(kBold | kDim)});
    CHECK(screen.cell(1, 5).attr == Attr{3, 4, kBold});
    CHECK(screen.cell(1, 3).utf8[0] == 'd');
    screen.add_flags(Rect{3, 8, 5, 9}, kDim);  // clipped at the screen's edges
    CHECK(screen.cell(3, 9).attr.flags == kDim);
}
