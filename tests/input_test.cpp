#include <doctest/doctest.h>

#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "app/keymap.hpp"
#include "ui/input.hpp"

using namespace mod;

namespace {

std::span<const std::byte> bytes(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

std::vector<InputEvent> feed_all(InputDecoder& d, std::string_view s) { return d.feed(bytes(s)); }

KeyEvent K(Key k, std::uint8_t mods = 0, char32_t ch = 0) { return KeyEvent{k, ch, mods}; }
KeyEvent ctrl(char c) { return KeyEvent{Key::CtrlLetter, static_cast<char32_t>(c), kCtrl}; }
KeyEvent alt(char c) { return KeyEvent{Key::Char, static_cast<char32_t>(c), kAlt}; }

struct Case {
    const char* terminal;
    std::string_view seq;
    KeyEvent key;
    std::optional<CommandId> command;
};

using C = CommandId;
constexpr std::uint8_t S = kShift;
constexpr std::uint8_t Ct = kCtrl;

// What each terminal sends for the keys the keymap binds.
const std::vector<Case> kCases = {
    // xterm (and anything xterm-compatible: GNOME Terminal, Konsole, kitty, alacritty).
    {"xterm", "\x13", ctrl('s'), C::Save},
    {"xterm", "\x1b[1;5H", K(Key::Home, Ct), C::MoveDocStart},
    {"xterm", "\x1b[1;5F", K(Key::End, Ct), C::MoveDocEnd},
    {"xterm", "\x1a", ctrl('z'), C::Undo},
    {"xterm", "\x19", ctrl('y'), C::Redo},
    {"xterm", "\x03", ctrl('c'), C::Copy},
    {"xterm", "\x16", ctrl('v'), C::Paste},
    {"xterm", "\x18", ctrl('x'), C::Cut},
    {"xterm", "\x06", ctrl('f'), C::Find},
    {"xterm", "\x01", ctrl('a'), C::MoveLineStart},
    {"xterm", "\x05", ctrl('e'), C::MoveLineEnd},
    {"xterm", "\x11", ctrl('q'), C::Exit},
    {"xterm", "\x07", ctrl('g'), C::GotoLine},
    {"xterm", "\x1b[1;2A", K(Key::Up, S), C::SelectUp},
    {"xterm", "\x1b[1;2B", K(Key::Down, S), C::SelectDown},
    {"xterm", "\x1b[1;2C", K(Key::Right, S), C::SelectRight},
    {"xterm", "\x1b[1;2D", K(Key::Left, S), C::SelectLeft},
    {"xterm", "\x1b[1;5D", K(Key::Left, Ct), C::MoveWordLeft},
    {"xterm", "\x1b[1;5C", K(Key::Right, Ct), C::MoveWordRight},
    {"xterm", "\x1b[1;6D", K(Key::Left, Ct | S), C::SelectWordLeft},
    {"xterm", "\x1b[1;6C", K(Key::Right, Ct | S), C::SelectWordRight},
    {"xterm", "\x1b[1;6H", K(Key::Home, Ct | S), C::SelectDocStart},
    {"xterm", "\x1b[1;6F", K(Key::End, Ct | S), C::SelectDocEnd},
    {"xterm", "\x1b[1;2H", K(Key::Home, S), C::SelectLineStart},
    {"xterm", "\x1b[1;2F", K(Key::End, S), C::SelectLineEnd},
    {"xterm", "\x1b" "f", alt('f'), std::nullopt},
    {"xterm", "\x1b" "e", alt('e'), std::nullopt},
    {"xterm", "\x1bv", alt('v'), std::nullopt},
    {"xterm", "\x1bo", alt('o'), std::nullopt},
    {"xterm", "\x1bh", alt('h'), std::nullopt},
    {"xterm", "\x1bx", alt('x'), C::ShowMenu},
    {"xterm", "\x1bm", alt('m'), std::nullopt},
    {"xterm", "\x1bOR", K(Key::F3), C::FindNext},
    {"xterm", "\x1b[1;2R", K(Key::F3, S), C::FindPrev},
    {"xterm", "\x1b[21~", K(Key::F10), C::ShowMenu},
    {"xterm", "\x1b[A", K(Key::Up), C::MoveUp},
    {"xterm", "\x1b[B", K(Key::Down), C::MoveDown},
    {"xterm", "\x1b[C", K(Key::Right), C::MoveRight},
    {"xterm", "\x1b[D", K(Key::Left), C::MoveLeft},
    {"xterm", "\x1bOA", K(Key::Up), C::MoveUp},  // application cursor mode
    {"xterm", "\x1b[H", K(Key::Home), C::MoveLineStart},
    {"xterm", "\x1b[F", K(Key::End), C::MoveLineEnd},
    {"xterm", "\x1bOH", K(Key::Home), C::MoveLineStart},
    {"xterm", "\x1bOF", K(Key::End), C::MoveLineEnd},
    {"xterm", "\x1b[5~", K(Key::PageUp), C::MovePageUp},
    {"xterm", "\x1b[6~", K(Key::PageDown), C::MovePageDown},
    {"xterm", "\x1b[5;2~", K(Key::PageUp, S), C::SelectPageUp},
    {"xterm", "\x1b[6;2~", K(Key::PageDown, S), C::SelectPageDown},
    {"xterm", "\x1b[3~", K(Key::Delete), C::DeleteForward},
    {"xterm", "\x1b[3;5~", K(Key::Delete, Ct), C::DeleteWordForward},
    {"xterm", "\r", K(Key::Enter), C::Newline},
    {"xterm", "\t", K(Key::Tab), C::InsertTab},
    {"xterm", "\x7f", K(Key::Backspace), C::DeleteBack},
    {"xterm", "\x1b[Z", K(Key::BackTab), C::Outdent},
    {"xterm", "\x1b[13;2u", K(Key::Enter, S), std::nullopt},
    {"xterm", "\x1b[27;2;13~", K(Key::Enter, S), std::nullopt},
    // rxvt.
    {"rxvt", "\x1b[7~", K(Key::Home), C::MoveLineStart},
    {"rxvt", "\x1b[8~", K(Key::End), C::MoveLineEnd},
    {"rxvt", "\x1b[7^", K(Key::Home, Ct), C::MoveDocStart},
    {"rxvt", "\x1b[8^", K(Key::End, Ct), C::MoveDocEnd},
    {"rxvt", "\x1b[7$", K(Key::Home, S), C::SelectLineStart},
    {"rxvt", "\x1b[8$", K(Key::End, S), C::SelectLineEnd},
    {"rxvt", "\x1b[7@", K(Key::Home, Ct | S), C::SelectDocStart},
    {"rxvt", "\x1b[c", K(Key::Right, S), C::SelectRight},
    {"rxvt", "\x1b[d", K(Key::Left, S), C::SelectLeft},
    {"rxvt", "\x1bOc", K(Key::Right, Ct), C::MoveWordRight},
    {"rxvt", "\x1bOd", K(Key::Left, Ct), C::MoveWordLeft},
    {"rxvt", "\x1b[13~", K(Key::F3), C::FindNext},
    {"rxvt", "\x1b[21~", K(Key::F10), C::ShowMenu},
    {"rxvt", "\x1b[3^", K(Key::Delete, Ct), C::DeleteWordForward},
    // Linux console.
    {"linux", "\x1b[1~", K(Key::Home), C::MoveLineStart},
    {"linux", "\x1b[4~", K(Key::End), C::MoveLineEnd},
    {"linux", "\x1b[[C", K(Key::F3), C::FindNext},
    {"linux", "\x1b[21~", K(Key::F10), C::ShowMenu},
    {"linux", "\x1b[25~", KeyEvent{}, std::nullopt},  // Shift+F3 arrives as F13: unbound, dropped
    // macOS Terminal with "Use Option as Meta key".
    {"macos", "\x1b" "f", alt('f'), std::nullopt},
    {"macos", "\x1b[1;2C", K(Key::Right, S), C::SelectRight},
    {"macos", "\x1b[H", K(Key::Home), C::MoveLineStart},
    {"macos", "\x1bOR", K(Key::F3), C::FindNext},
    {"macos", "\x1b[1;2R", K(Key::F3, S), C::FindPrev},
    {"macos", "\x1b" "b", alt('b'), std::nullopt},  // Option+Left as word-left in some profiles: unbound
    // Windows Terminal.
    {"wt", "\x08", K(Key::Backspace, Ct), C::DeleteWordBack},
    {"wt", "\x7f", K(Key::Backspace), C::DeleteBack},
    {"wt", "\x1b[1;5H", K(Key::Home, Ct), C::MoveDocStart},
    {"wt", "\x1b[1;2R", K(Key::F3, S), C::FindPrev},
    {"wt", "\x1b" "X", KeyEvent{Key::Char, U'X', kAlt}, C::ShowMenu},  // Alt+Shift+X still shows the menu
    {"wt", "\x1b[21~", K(Key::F10), C::ShowMenu},
};

std::vector<InputEvent> decode_whole(std::string_view seq) {
    InputDecoder d;
    auto ev = d.feed(bytes(seq));
    auto rest = d.timeout();
    ev.insert(ev.end(), rest.begin(), rest.end());
    return ev;
}

}  // namespace

TEST_CASE("every binding decodes and maps, as each terminal sends it") {
    Keymap km;
    for (const Case& c : kCases) {
        CAPTURE(c.terminal);
        CAPTURE(std::string(c.seq));
        const auto ev = decode_whole(c.seq);
        if (c.key == KeyEvent{} && !c.command) {
            CHECK(ev.empty());
            continue;
        }
        REQUIRE(ev.size() == 1);
        const auto* k = std::get_if<KeyEvent>(&ev[0]);
        REQUIRE(k != nullptr);
        CHECK(*k == c.key);
        CHECK(km.lookup(*k) == c.command);
    }
}

TEST_CASE("split reads at every byte position give the same events") {
    for (const Case& c : kCases) {
        CAPTURE(std::string(c.seq));
        const auto whole = decode_whole(c.seq);
        for (std::size_t cut = 1; cut < c.seq.size(); ++cut) {
            InputDecoder d;
            auto ev = d.feed(bytes(c.seq.substr(0, cut)));
            auto more = d.feed(bytes(c.seq.substr(cut)));
            ev.insert(ev.end(), more.begin(), more.end());
            CHECK(ev == whole);
            CHECK_FALSE(d.pending_timeout().has_value());
        }
    }
    // A run of keys split byte by byte.
    const std::string run = "a\x1b[1;5Hb\xc3\xa9\x1bOR\x13";
    InputDecoder whole_d;
    const auto whole = whole_d.feed(bytes(run));
    InputDecoder d;
    std::vector<InputEvent> ev;
    for (char ch : run) {
        auto part = d.feed(bytes(std::string_view(&ch, 1)));
        ev.insert(ev.end(), part.begin(), part.end());
    }
    CHECK(ev == whole);
    CHECK(whole.size() == 6);
}

TEST_CASE("a lone Esc is reported only by timeout; Alt+key is not") {
    InputDecoder d;
    CHECK(feed_all(d, "\x1b").empty());
    REQUIRE(d.pending_timeout().has_value());
    CHECK(*d.pending_timeout() == InputDecoder::kEscTimeout);
    const auto ev = d.timeout();
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == K(Key::Escape));
    CHECK_FALSE(d.pending_timeout().has_value());

    // ESC then a letter within the timeout is Alt+letter.
    CHECK(feed_all(d, "\x1b").empty());
    const auto alt_f = feed_all(d, "f");
    REQUIRE(alt_f.size() == 1);
    CHECK(std::get<KeyEvent>(alt_f[0]) == alt('f'));

    // ESC, timeout, then the letter: Escape and a plain letter.
    CHECK(feed_all(d, "\x1b").empty());
    CHECK(std::get<KeyEvent>(d.timeout()[0]) == K(Key::Escape));
    CHECK(std::get<KeyEvent>(feed_all(d, "f")[0]) == KeyEvent{Key::Char, U'f', 0});

    // ESC ESC: the first is a lone Escape at once.
    const auto two = feed_all(d, "\x1b\x1b");
    REQUIRE(two.size() == 1);
    CHECK(std::get<KeyEvent>(two[0]) == K(Key::Escape));
    CHECK(std::get<KeyEvent>(d.timeout()[0]) == K(Key::Escape));

    // An unfinished CSI resolves to Alt+[ when nothing follows.
    CHECK(feed_all(d, "\x1b[").empty());
    CHECK(std::get<KeyEvent>(d.timeout()[0]) == alt('['));
}

TEST_CASE("bracketed paste") {
    SUBCASE("paste containing ESC bytes and sequences") {
        InputDecoder d;
        const auto ev = feed_all(d, "\x1b[200~a\x1b" "b\x1b[A\r\nz\x1b[201~q");
        REQUIRE(ev.size() == 2);
        CHECK(std::get<PasteEvent>(ev[0]).bytes == "a\x1b" "b\x1b[A\r\nz");
        CHECK(std::get<KeyEvent>(ev[1]) == KeyEvent{Key::Char, U'q', 0});
    }
    SUBCASE("terminator split at every position") {
        const std::string all = "\x1b[200~hello\x1b[201~";
        for (std::size_t cut = 1; cut < all.size(); ++cut) {
            InputDecoder d;
            auto ev = d.feed(bytes(std::string_view(all).substr(0, cut)));
            CHECK_FALSE((d.pending_timeout() < InputDecoder::kPasteIdle && cut > 6));  // inside a paste only the idle timeout
            auto more = d.feed(bytes(std::string_view(all).substr(cut)));
            ev.insert(ev.end(), more.begin(), more.end());
            REQUIRE(ev.size() == 1);
            CHECK(std::get<PasteEvent>(ev[0]).bytes == "hello");
        }
    }
    SUBCASE("a paste waits for its terminator across reads, only the long idle timeout pending") {
        InputDecoder d;
        CHECK(feed_all(d, "\x1b[200~abc\x1b[20").empty());
        CHECK(d.pending_timeout() == InputDecoder::kPasteIdle);
        const auto ev = feed_all(d, "1~");
        REQUIRE(ev.size() == 1);
        CHECK(std::get<PasteEvent>(ev[0]).bytes == "abc");
    }
}

TEST_CASE("text: UTF-8, invalid bytes and control characters") {
    InputDecoder d;
    auto ev = feed_all(d, "a\xc3\xa9\xe2\x82\xac\xf0\x9f\x98\x80");
    REQUIRE(ev.size() == 4);
    CHECK(std::get<KeyEvent>(ev[0]).ch == U'a');
    CHECK(std::get<KeyEvent>(ev[1]).ch == U'é');
    CHECK(std::get<KeyEvent>(ev[2]).ch == U'€');
    CHECK(std::get<KeyEvent>(ev[3]).ch == U'\U0001F600');

    // Invalid bytes become U+FFFD, one event each, and are never dropped silently.
    ev = feed_all(d, "\xff\xc0\x80x");
    REQUIRE(ev.size() == 4);
    for (int i = 0; i < 3; ++i) CHECK(std::get<KeyEvent>(ev[static_cast<std::size_t>(i)]).ch == U'�');
    CHECK(std::get<KeyEvent>(ev[3]).ch == U'x');

    // A truncated sequence waits, then becomes U+FFFD on timeout.
    CHECK(feed_all(d, "\xe2\x82").empty());
    REQUIRE(d.pending_timeout().has_value());
    ev = d.timeout();
    REQUIRE(ev.size() == 2);
    CHECK(std::get<KeyEvent>(ev[0]).ch == U'�');

    // A lead byte followed by a non-continuation byte does not wait.
    ev = feed_all(d, "\xc3" "a");
    REQUIRE(ev.size() == 2);
    CHECK(std::get<KeyEvent>(ev[0]).ch == U'�');
    CHECK(std::get<KeyEvent>(ev[1]).ch == U'a');

    // Alt + a multi-byte character.
    ev = feed_all(d, "\x1b\xc3\xa9");
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'é', kAlt});

    CHECK(to_utf8(U'€') == "\xe2\x82\xac");
    CHECK(to_utf8(0xD800) == "\xef\xbf\xbd");
}

TEST_CASE("unknown sequences are consumed and dropped, never inserted") {
    InputDecoder d;
    auto ev = feed_all(d, "\x1b[99~\x1b[?1;2c\x1b[200;5z" "a");
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'a', 0});
    ev = feed_all(d, "\x1bOX" "b");
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]).ch == U'b');
}

TEST_CASE("keymap: text is inserted, labels name the first binding") {
    Keymap km;
    CHECK_FALSE(km.lookup(KeyEvent{Key::Char, U'x', 0}).has_value());
    CHECK_FALSE(km.lookup(KeyEvent{Key::Char, U'X', kShift}).has_value());
    CHECK(km.binding_label(C::Save) == "Ctrl+S");
    CHECK(km.binding_label(C::Exit) == "Ctrl+Q");
    CHECK(km.binding_label(C::FindPrev) == "Shift+F3");
    CHECK(km.binding_label(C::MoveDocStart) == "Ctrl+Home");
    CHECK(km.binding_label(C::ShowMenu).starts_with("Alt+X"));
    // Menu-only commands have no key.
    for (C c : {C::SaveAs, C::ClearHistory, C::TrimHistory, C::UndoHistory, C::NextBranch, C::PrevBranch, C::UserSettings, C::RecentSetting1,
                C::ToggleLineNumbers, C::ToggleSyntax, C::KeyBindings, C::About, C::OpenMenuFile, C::OpenMenuHelp}) {
        CHECK(km.binding_label(c).empty());
    }
    // Esc is never bound: it never quits.
    CHECK_FALSE(km.lookup(K(Key::Escape)).has_value());
}


TEST_CASE("a paste that never ends does not swallow the keys after it") {
    InputDecoder d;
    CHECK(feed_all(d, "\x1b[200~abc").empty());
    REQUIRE(d.pending_timeout() == InputDecoder::kPasteIdle);
    const auto ended = d.timeout();  // the terminal went quiet: the paste is over
    REQUIRE(ended.size() == 1);
    CHECK(std::get<PasteEvent>(ended[0]).bytes == "abc");
    const auto ev = feed_all(d, "q");
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'q', 0});
}

TEST_CASE("a huge paste arrives in bounded pieces, nothing lost") {
    InputDecoder d;
    std::string body;
    for (int i = 0; body.size() < 3 * InputDecoder::kPasteChunk + 123; ++i) body += "line " + std::to_string(i) + "\n";
    std::string got;
    std::size_t largest = 0;
    auto take = [&](const std::vector<InputEvent>& events) {
        for (const InputEvent& e : events) {
            const auto& paste = std::get<PasteEvent>(e);
            largest = std::max(largest, paste.bytes.size());
            got += paste.bytes;
        }
    };
    take(d.feed(bytes("\x1b[200~")));
    for (std::size_t at = 0; at < body.size(); at += 65536) take(d.feed(bytes(std::string_view(body).substr(at, 65536))));
    take(d.feed(bytes("\x1b[201~")));
    CHECK(got == body);
    CHECK(largest <= InputDecoder::kPasteChunk);
}

TEST_CASE("terminal replies (OSC and DCS strings) are skipped, never typed; Alt keys still work") {
    InputDecoder d;
    auto ev = feed_all(d, "\x1b]11;rgb:0000/0000/0000\x07x\x1bP1$r0m\x1b\\y\x1b]52;c;QUJD\x1b\\z");
    REQUIRE(ev.size() == 3);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'x', 0});
    CHECK(std::get<KeyEvent>(ev[1]) == KeyEvent{Key::Char, U'y', 0});
    CHECK(std::get<KeyEvent>(ev[2]) == KeyEvent{Key::Char, U'z', 0});
    // Split across reads, the string still waits for its end.
    InputDecoder split;
    CHECK(split.feed(bytes("\x1b]0;ti")).empty());
    ev = feed_all(split, "tle\x07k");
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'k', 0});
    // One that never ends is dropped when the terminal goes quiet, not typed.
    InputDecoder open;
    CHECK(open.feed(bytes("\x1b]52;c;AAAA")).empty());
    CHECK(open.pending_timeout() == InputDecoder::kPasteIdle);
    CHECK(open.timeout().empty());
    ev = feed_all(open, "m");
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'm', 0});
    // Alt+] and Alt+Shift+P are keys, alone or followed by a letter.
    InputDecoder alt;
    CHECK(alt.feed(bytes("\x1b]")).empty());
    ev = alt.timeout();
    REQUIRE(ev.size() == 1);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U']', kAlt});
    ev = feed_all(alt, "\x1bPq");
    REQUIRE(ev.size() == 2);
    CHECK(std::get<KeyEvent>(ev[0]) == KeyEvent{Key::Char, U'P', kAlt});
}

TEST_CASE("CSI u with a control code point is not typed as text") {
    InputDecoder d;
    for (const char* seq : {"\x1b[1u", "\x1b[31u", "\x1b[155u", "\x1b[128u"}) {
        CAPTURE(seq);
        for (const InputEvent& e : feed_all(d, seq)) {
            const auto* k = std::get_if<KeyEvent>(&e);
            REQUIRE(k != nullptr);
            CHECK(k->key != Key::Char);
        }
    }
}

TEST_CASE("a terminal reply longer than the limit is dropped whole, however it arrives") {
    for (const std::string_view end : {std::string_view("\a"), std::string_view("\x1b\\")}) {
        CAPTURE(end);
        InputDecoder d;
        std::string reply = "\x1b]52;c;" + std::string(InputDecoder::kMaxStringBytes + 6000, 'A');
        reply += end;
        reply += "x";
        std::vector<InputEvent> events;
        for (std::size_t i = 0; i < reply.size(); i += 4096) {
            for (InputEvent& e : feed_all(d, std::string_view(reply).substr(i, 4096))) events.push_back(std::move(e));
        }
        REQUIRE(events.size() == 1);
        CHECK(std::get<KeyEvent>(events[0]) == KeyEvent{Key::Char, U'x'});
    }
}

TEST_CASE("an over-long reply's ESC \\ split across reads still ends it") {
    InputDecoder d;
    const std::string body = "\x1b]52;c;" + std::string(InputDecoder::kMaxStringBytes + 10, 'A') + "\x1b";
    CHECK(feed_all(d, body).empty());
    const auto rest = feed_all(d, "\\y");
    REQUIRE(rest.size() == 1);
    CHECK(std::get<KeyEvent>(rest[0]) == KeyEvent{Key::Char, U'y'});
}

TEST_CASE("an over-long reply that never ends is over after a silence") {
    InputDecoder d;
    CHECK(feed_all(d, "\x1b]52;c;" + std::string(InputDecoder::kMaxStringBytes + 10, 'A')).empty());
    CHECK(d.pending_timeout() == InputDecoder::kPasteIdle);
    CHECK(d.timeout().empty());
    const auto after = feed_all(d, "z");
    REQUIRE(after.size() == 1);
    CHECK(std::get<KeyEvent>(after[0]) == KeyEvent{Key::Char, U'z'});
}

TEST_CASE("a paste is never cut between CR and LF, even when a chunk ends exactly at the CR") {
    InputDecoder d;
    std::vector<std::string> pieces;
    auto collect = [&](const std::vector<InputEvent>& events) {
        for (const InputEvent& e : events) pieces.push_back(std::get<PasteEvent>(e).bytes);
    };
    collect(feed_all(d, "\x1b[200~" + std::string(InputDecoder::kPasteChunk - 1, 'a') + "\r"));
    collect(feed_all(d, "\nb\x1b[201~"));
    std::string joined;
    for (std::size_t i = 0; i < pieces.size(); ++i) {
        CAPTURE(i);
        if (i + 1 < pieces.size()) CHECK_FALSE((pieces[i].ends_with('\r') && pieces[i + 1].starts_with('\n')));
        joined += pieces[i];
    }
    CHECK(joined == std::string(InputDecoder::kPasteChunk - 1, 'a') + "\r\nb");
}

TEST_CASE("a paste handed on in pieces marks every piece but the last, which may be empty") {
    InputDecoder d;
    std::vector<PasteEvent> pieces;
    auto collect = [&](const std::vector<InputEvent>& events) {
        for (const InputEvent& e : events) pieces.push_back(std::get<PasteEvent>(e));
    };
    collect(feed_all(d, "\x1b[200~" + std::string(InputDecoder::kPasteChunk + 1, 'a')));
    collect(feed_all(d, "\x1b[201~"));
    REQUIRE(pieces.size() == 2);
    CHECK(pieces[0].more);
    CHECK_FALSE(pieces[1].more);
    InputDecoder whole;
    const auto one = feed_all(whole, "\x1b[200~short\x1b[201~");
    REQUIRE(one.size() == 1);
    CHECK(std::get<PasteEvent>(one[0]) == PasteEvent{"short", false});
}
