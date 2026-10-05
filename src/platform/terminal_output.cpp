#include "platform/terminal_output.hpp"

#include <algorithm>
#include <array>
#include <charconv>
#include <format>
#include <span>

#include "text/utf8.hpp"
#include "ui/screen.hpp"

namespace mod {
namespace {

class XtermOutput final : public TerminalOutput {
public:
    TerminalMode mode() const noexcept override { return TerminalMode::xterm; }
    // The terminal's title is pushed on its stack first and popped last (CSI 22/23 t).
    std::string_view enter() const noexcept override { return "\x1b[22;0t\x1b[?1049h\x1b[?2004h\x1b[?25l"; }
    std::string_view leave() const noexcept override { return "\x1b[?2004l\x1b[?1049l\x1b[?25h\x1b[0m\x1b[0 q\x1b[23;0t"; }
    std::string_view begin_frame() const noexcept override { return "\x1b[?2026h\x1b[?25l\x1b[?7l"; }
    std::string_view end_frame() const noexcept override { return "\x1b[0m\x1b[?7h"; }
    std::string_view end_sync() const noexcept override { return "\x1b[?2026l"; }
    std::string_view show_cursor() const noexcept override { return "\x1b[?25h"; }
    std::string_view cursor_shape(CursorStyle style) const noexcept override {
        switch (style) {
            case CursorStyle::bar: return "\x1b[6 q";
            case CursorStyle::bar_blink: return "\x1b[5 q";
            case CursorStyle::block: return "\x1b[2 q";
            case CursorStyle::block_blink: return "\x1b[1 q";
            case CursorStyle::underline: return "\x1b[4 q";
            case CursorStyle::underline_blink: return "\x1b[3 q";
        }
        return {};
    }
    void append_attr(std::string& out, const Attr& a) const override {
        out += "\x1b[0";
        if (a.flags & kBold) out += ";1";
        if (a.flags & kDim) out += ";2";
        if (a.flags & kItalic) out += ";3";
        if (a.flags & kUnderline) out += ";4";
        if (a.flags & kReverse) out += ";7";
        if (a.flags & kStrike) out += ";9";
        if (a.fg != kDefaultColor) out += std::format(";{}", a.fg < 8 ? 30 + a.fg : 90 + (a.fg - 8));
        if (a.bg != kDefaultColor) out += std::format(";{}", a.bg < 8 ? 40 + a.bg : 100 + (a.bg - 8));
        out += 'm';
    }
    void append_title(std::string& out, std::string_view title) const override {
        out += "\x1b]0;";
        for (std::size_t i = 0; i < title.size();) {
            const Decoded d = decode(std::as_bytes(std::span(title.data() + i, title.size() - i)));
            const std::size_t len = std::max<std::size_t>(1, d.len);
            const bool control = d.cp < 0x20 || (d.cp >= 0x7F && d.cp <= 0x9F);
            if (d.valid && !control) {
                out.append(title.substr(i, len));
            } else {
                out += '?';
            }
            i += len;
        }
        out += '\a';
    }
};

// Strict VT100: cursor positioning, erase, autowrap and SGR 0/1/4/7 only.
class Vt100Output final : public TerminalOutput {
public:
    TerminalMode mode() const noexcept override { return TerminalMode::vt100; }
    std::string_view enter() const noexcept override { return {}; }
    std::string_view leave() const noexcept override { return "\x1b[0m\x1b[2J\x1b[H"; }
    std::string_view begin_frame() const noexcept override { return "\x1b[?7l"; }
    std::string_view end_frame() const noexcept override { return "\x1b[0m\x1b[?7h"; }
    std::string_view end_sync() const noexcept override { return {}; }
    std::string_view show_cursor() const noexcept override { return {}; }
    std::string_view cursor_shape(CursorStyle) const noexcept override { return {}; }  // a VT100 has one cursor
    void append_title(std::string&, std::string_view) const override {}
    void append_attr(std::string& out, const Attr& a) const override {
        out += "\x1b[0";
        if (a.flags & kBold) out += ";1";
        if (a.flags & kUnderline) out += ";4";
        if (a.flags & kReverse) out += ";7";
        out += 'm';
    }
};

bool starts_with_any(std::string_view s, std::initializer_list<std::string_view> prefixes) {
    for (std::string_view p : prefixes)
        if (s.starts_with(p)) return true;
    return false;
}

}  // namespace

void TerminalOutput::append_move(std::string& out, int row, int col) const { out += std::format("\x1b[{};{}H", row + 1, col + 1); }

const TerminalOutput& output_for(TerminalMode mode) {
    static const XtermOutput xterm;
    static const Vt100Output vt100;
    if (mode == TerminalMode::vt100) return vt100;
    return xterm;
}

std::optional<TerminalMode> mode_from_environment(const std::function<const char*(const char*)>& env) {
    const char* term_c = env("TERM");
    const std::string_view term = term_c != nullptr ? term_c : "";
    if (term == "dumb" || (term.starts_with("vt") && term.size() > 2)) return TerminalMode::vt100;
    if (term == "linux" || starts_with_any(term, {"xterm", "screen", "tmux", "rxvt", "alacritty", "kitty", "foot", "wezterm",
                                                  "konsole", "gnome", "st-", "putty", "iterm", "contour"}))
        return TerminalMode::xterm;
    for (const char* name : {"COLORTERM", "TERM_PROGRAM", "WT_SESSION"}) {
        if (const char* v = env(name); v != nullptr && *v != '\0') return TerminalMode::xterm;
    }
    return std::nullopt;
}

std::optional<std::pair<std::size_t, std::size_t>> find_device_attributes(std::string_view bytes) {
    for (std::size_t at = bytes.find("\x1b[?"); at != std::string_view::npos; at = bytes.find("\x1b[?", at + 1)) {
        std::size_t i = at + 3;
        while (i < bytes.size() && ((bytes[i] >= '0' && bytes[i] <= '9') || bytes[i] == ';')) ++i;
        if (i < bytes.size() && bytes[i] == 'c' && i > at + 3) return std::pair{at, i + 1};
    }
    return std::nullopt;
}

std::optional<TerminalMode> mode_from_device_attributes(std::string_view bytes) {
    const auto range = find_device_attributes(bytes);
    if (!range) return std::nullopt;
    const std::string_view reply = bytes.substr(range->first + 3, range->second - range->first - 4);
    int cls = 0;
    const auto [end, ec] = std::from_chars(reply.data(), reply.data() + reply.size(), cls);
    if (ec != std::errc{} || end == reply.data()) return std::nullopt;
    if (cls >= 60) return TerminalMode::xterm;
    if (cls == 1 || cls == 2 || cls == 4 || cls == 6) return TerminalMode::vt100;
    return std::nullopt;
}

}  // namespace mod
