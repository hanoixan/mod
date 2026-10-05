#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

namespace mod {

enum class Key : std::uint8_t {
    Char,
    Enter,
    Tab,
    BackTab,
    Backspace,
    Delete,
    Insert,
    Escape,
    Up,
    Down,
    Left,
    Right,
    Home,
    End,
    PageUp,
    PageDown,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    CtrlLetter,
};

// KeyEvent::mods bits, as in xterm's modifier parameter minus one.
inline constexpr std::uint8_t kShift = 1u << 0;
inline constexpr std::uint8_t kAlt = 1u << 1;
inline constexpr std::uint8_t kCtrl = 1u << 2;

// `ch` is the code point for `Char` (U+FFFD for an invalid byte) and the lowercase
// letter for `CtrlLetter` (or one of `@\]^_` and space).
struct KeyEvent {
    Key key = Key::Char;
    char32_t ch = 0;
    std::uint8_t mods = 0;

    friend bool operator==(const KeyEvent&, const KeyEvent&) = default;
};

struct PasteEvent {
    std::string bytes;
    bool more = false;  // more of the same paste follows in later events

    friend bool operator==(const PasteEvent&, const PasteEvent&) = default;
};

using InputEvent = std::variant<KeyEvent, PasteEvent>;

// The UTF-8 bytes of a code point (U+FFFD for one that cannot be encoded).
std::string to_utf8(char32_t cp);

// Decodes the terminal byte stream: xterm CSI and SS3 with modifiers, ESC-prefix Alt,
// control characters, UTF-8 text and bracketed paste. Main thread.
class InputDecoder {
public:
    static constexpr std::chrono::milliseconds kEscTimeout{25};
    // A paste, or a terminal string, still unterminated after this much silence is over.
    static constexpr std::chrono::milliseconds kPasteIdle{1500};
    // A long paste is handed on in pieces of at most this many bytes.
    static constexpr std::size_t kPasteChunk = std::size_t{1} << 20;
    static constexpr std::size_t kMaxStringBytes = 64 * 1024;

    std::vector<InputEvent> feed(std::span<const std::byte> bytes);
    // How long to wait before calling `timeout`; nullopt when nothing is pending.
    std::optional<std::chrono::milliseconds> pending_timeout() const;
    std::vector<InputEvent> timeout();

private:
    enum class Parse { done, need_more };
    Parse parse_one(std::vector<InputEvent>& out);
    Parse parse_escape(std::vector<InputEvent>& out);
    Parse parse_csi(std::vector<InputEvent>& out);
    Parse parse_ss3(std::vector<InputEvent>& out);
    // A key at buf_[pos] (not ESC); `consumed` is set to its length.
    Parse parse_plain(std::size_t pos, std::uint8_t extra_mods, std::vector<InputEvent>& out, std::size_t& consumed);
    void feed_paste(std::vector<InputEvent>& out);
    void deliver_paste_chunks(std::vector<InputEvent>& out);
    void end_paste(std::vector<InputEvent>& out);
    Parse skip_string();
    bool in_string() const;
    // Drops bytes up to the end of an over-long string; false when more are needed.
    bool discard_string();

    std::string buf_;  // undecoded bytes
    bool in_paste_ = false;
    bool discarding_ = false;  // in an over-long terminal string, dropped up to its end
    std::string paste_;
    bool paste_handed_on_ = false;  // pieces of this paste went out already
};

}  // namespace mod
