#include "ui/input.hpp"

#include <algorithm>
#include <string_view>

#include "text/utf8.hpp"

namespace mod {
namespace {

constexpr char kEsc = '\x1b';
constexpr std::string_view kPasteEnd = "\x1b[201~";
constexpr std::size_t kMaxSequence = 64;  // longer "sequences" are garbage and dropped
constexpr char32_t kReplacement = 0xFFFD;

KeyEvent key(Key k, std::uint8_t mods = 0, char32_t ch = 0) { return KeyEvent{k, ch, mods}; }

// xterm's modifier parameter is 1 + bits (Shift 1, Alt 2, Ctrl 4; Meta 8 is ignored).
std::uint8_t mods_from_param(int param) {
    if (param <= 1) return 0;
    return static_cast<std::uint8_t>((param - 1) & (kShift | kAlt | kCtrl));
}

// UTF-8 sequence length from its lead byte; 0 for a byte that cannot lead.
std::size_t utf8_length(unsigned char lead) {
    if (lead < 0x80) return 1;
    if (lead >= 0xC2 && lead <= 0xDF) return 2;
    if (lead >= 0xE0 && lead <= 0xEF) return 3;
    if (lead >= 0xF0 && lead <= 0xF4) return 4;
    return 0;
}

std::optional<Key> tilde_key(int n) {
    switch (n) {
        case 1:
        case 7: return Key::Home;
        case 2: return Key::Insert;
        case 3: return Key::Delete;
        case 4:
        case 8: return Key::End;
        case 5: return Key::PageUp;
        case 6: return Key::PageDown;
        case 11: return Key::F1;
        case 12: return Key::F2;
        case 13: return Key::F3;
        case 14: return Key::F4;
        case 15: return Key::F5;
        case 17: return Key::F6;
        case 18: return Key::F7;
        case 19: return Key::F8;
        case 20: return Key::F9;
        case 21: return Key::F10;
        case 23: return Key::F11;
        case 24: return Key::F12;
        default: return std::nullopt;
    }
}

std::optional<Key> final_key(char c) {
    switch (c) {
        case 'A': return Key::Up;
        case 'B': return Key::Down;
        case 'C': return Key::Right;
        case 'D': return Key::Left;
        case 'H': return Key::Home;
        case 'F': return Key::End;
        case 'P': return Key::F1;
        case 'Q': return Key::F2;
        case 'R': return Key::F3;
        case 'S': return Key::F4;
        default: return std::nullopt;
    }
}

// A key reported by code point (CSI u, or xterm's modifyOtherKeys `CSI 27;m;code~`).
// Whether ESC `intro` `first` opens a terminal string rather than being Alt+`intro`: an OSC
// reply starts with a number, a DCS reply with a digit, '$', '+' or '!'. (ESC followed by
// a letter is also how Alt+Shift+P and the like arrive, so nothing else is taken.)
bool opens_string(char intro, char first) {
    const bool digit = first >= '0' && first <= '9';
    if (intro == ']') return digit;
    if (intro == 'P') return digit || first == '$' || first == '+' || first == '!';
    return false;
}

// A CSI u code point as a key; a control code point (C0 other than the four above, DEL, C1)
// is no key, never text.
std::optional<KeyEvent> code_key(int code, std::uint8_t mods) {
    switch (code) {
        case 13: return key(Key::Enter, mods);
        case 9: return key(mods & kShift ? Key::BackTab : Key::Tab, static_cast<std::uint8_t>(mods & ~kShift));
        case 127: return key(Key::Backspace, mods);
        case 27: return key(Key::Escape, mods);
        default: break;
    }
    if (code < 0x20 || (code >= 0x7F && code <= 0x9F)) return std::nullopt;
    if (mods & kCtrl) {
        if (code >= 'a' && code <= 'z') return key(Key::CtrlLetter, mods, static_cast<char32_t>(code));
        if (code >= 'A' && code <= 'Z') return key(Key::CtrlLetter, mods, static_cast<char32_t>(code - 'A' + 'a'));
    }
    return key(Key::Char, mods, code > 0 ? static_cast<char32_t>(code) : kReplacement);
}

std::vector<int> params_of(std::string_view s) {
    std::vector<int> out;
    int cur = 0;
    bool any = false;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            if (cur < 100000) cur = cur * 10 + (c - '0');
            any = true;
        } else if (c == ';' || c == ':') {
            out.push_back(any ? cur : 0);
            cur = 0;
            any = false;
        }
    }
    out.push_back(any ? cur : 0);
    return out;
}

}  // namespace

std::string to_utf8(char32_t cp) {
    if (cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) cp = kReplacement;
    std::string out;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return out;
}

std::vector<InputEvent> InputDecoder::feed(std::span<const std::byte> bytes) {
    buf_.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    std::vector<InputEvent> out;
    while (!buf_.empty()) {
        if (in_paste_) {
            feed_paste(out);
            if (in_paste_) break;
            continue;
        }
        if (discarding_) {
            if (!discard_string()) break;
            continue;
        }
        if (parse_one(out) == Parse::need_more) break;
    }
    return out;
}

// Hands on the paste so far in pieces of at most kPasteChunk bytes, each cut before a
// UTF-8 continuation byte and never between CR and LF; the rest waits for more.
void InputDecoder::deliver_paste_chunks(std::vector<InputEvent>& out) {
    // Only with a byte past the cut in hand can the cut be checked against it.
    while (paste_.size() > kPasteChunk) {
        std::size_t cut = kPasteChunk;
        while (cut > 0 && ((static_cast<unsigned char>(paste_[cut]) & 0xC0) == 0x80 || (paste_[cut - 1] == '\r' && paste_[cut] == '\n'))) --cut;
        if (cut == 0) cut = kPasteChunk;  // not text: cut anywhere
        out.emplace_back(PasteEvent{paste_.substr(0, cut), true});
        paste_.erase(0, cut);
        paste_handed_on_ = true;
    }
}

void InputDecoder::end_paste(std::vector<InputEvent>& out) {
    in_paste_ = false;
    // A paste handed on in pieces always gets its last one, so its end is known.
    if (!paste_.empty() || paste_handed_on_) out.emplace_back(PasteEvent{std::move(paste_), false});
    paste_.clear();
    paste_handed_on_ = false;
}

void InputDecoder::feed_paste(std::vector<InputEvent>& out) {
    const std::size_t end = buf_.find(kPasteEnd);
    if (end != std::string::npos) {
        paste_.append(buf_, 0, end);
        buf_.erase(0, end + kPasteEnd.size());
        deliver_paste_chunks(out);
        end_paste(out);
        return;
    }
    // Keep a tail that may be the start of a split terminator.
    std::size_t keep = 0;
    for (std::size_t k = std::min(buf_.size(), kPasteEnd.size() - 1); k > 0; --k) {
        if (std::string_view(buf_).substr(buf_.size() - k) == kPasteEnd.substr(0, k)) {
            keep = k;
            break;
        }
    }
    paste_.append(buf_, 0, buf_.size() - keep);
    buf_.erase(0, buf_.size() - keep);
    deliver_paste_chunks(out);
}

InputDecoder::Parse InputDecoder::parse_one(std::vector<InputEvent>& out) {
    if (buf_[0] == kEsc) return parse_escape(out);
    std::size_t consumed = 0;
    if (parse_plain(0, 0, out, consumed) == Parse::need_more) return Parse::need_more;
    buf_.erase(0, consumed);
    return Parse::done;
}

InputDecoder::Parse InputDecoder::parse_plain(std::size_t pos, std::uint8_t extra, std::vector<InputEvent>& out,
                                              std::size_t& consumed) {
    const auto b = static_cast<unsigned char>(buf_[pos]);
    consumed = 1;
    switch (b) {
        case 0x0D:
        case 0x0A: out.emplace_back(key(Key::Enter, extra)); return Parse::done;
        case 0x09: out.emplace_back(key(Key::Tab, extra)); return Parse::done;
        case 0x7F: out.emplace_back(key(Key::Backspace, extra)); return Parse::done;
        case 0x08: out.emplace_back(key(Key::Backspace, static_cast<std::uint8_t>(extra | kCtrl))); return Parse::done;
        case 0x00: out.emplace_back(key(Key::CtrlLetter, static_cast<std::uint8_t>(extra | kCtrl), U' ')); return Parse::done;
        default: break;
    }
    if (b < 0x20) {
        const char32_t ch = b <= 0x1A ? static_cast<char32_t>('a' + b - 1) : static_cast<char32_t>('@' + b);
        out.emplace_back(key(Key::CtrlLetter, static_cast<std::uint8_t>(extra | kCtrl), ch));
        return Parse::done;
    }
    const std::size_t want = utf8_length(b);
    if (want > 1 && buf_.size() - pos < want) {
        // Wait for the rest only while every byte so far is a continuation byte.
        bool prefix = true;
        for (std::size_t i = pos + 1; i < buf_.size(); ++i) prefix = prefix && (static_cast<unsigned char>(buf_[i]) & 0xC0) == 0x80;
        if (prefix) return Parse::need_more;
    }
    const auto rest = std::as_bytes(std::span(buf_.data() + pos, buf_.size() - pos));
    const Decoded d = decode(rest);
    consumed = std::max<std::size_t>(1, d.len);
    out.emplace_back(key(Key::Char, extra, d.valid ? d.cp : kReplacement));
    return Parse::done;
}

InputDecoder::Parse InputDecoder::parse_escape(std::vector<InputEvent>& out) {
    if (buf_.size() < 2) return Parse::need_more;  // a lone ESC waits for `timeout`
    const char next = buf_[1];
    if (next == '[') return parse_csi(out);
    if (next == 'O') return parse_ss3(out);
    if (next == ']' || next == 'P') {
        if (buf_.size() < 3) return Parse::need_more;
        if (opens_string(next, buf_[2])) return skip_string();
    }
    if (next == kEsc) {
        // ESC ESC: the first is a lone Escape.
        out.emplace_back(key(Key::Escape));
        buf_.erase(0, 1);
        return Parse::done;
    }
    std::size_t consumed = 0;
    if (parse_plain(1, kAlt, out, consumed) == Parse::need_more) return Parse::need_more;
    buf_.erase(0, 1 + consumed);
    return Parse::done;
}

// OSC and DCS strings (a terminal's replies) end with BEL or ESC \\; they
// are dropped whole. Past kMaxStringBytes the rest is dropped as it arrives, not held.
InputDecoder::Parse InputDecoder::skip_string() {
    for (std::size_t i = 2; i < buf_.size(); ++i) {
        if (buf_[i] == '\a') {
            buf_.erase(0, i + 1);
            return Parse::done;
        }
        if (buf_[i] == kEsc && i + 1 < buf_.size() && buf_[i + 1] == '\\') {
            buf_.erase(0, i + 2);
            return Parse::done;
        }
    }
    if (buf_.size() < kMaxStringBytes) return Parse::need_more;
    buf_.erase(0, 2);  // no terminator in what is held, but an ESC at its end may start one
    discarding_ = true;
    return Parse::done;
}

bool InputDecoder::discard_string() {
    for (std::size_t i = 0; i < buf_.size(); ++i) {
        if (buf_[i] == '\a') {
            buf_.erase(0, i + 1);
            discarding_ = false;
            return true;
        }
        if (buf_[i] == kEsc && i + 1 < buf_.size() && buf_[i + 1] == '\\') {
            buf_.erase(0, i + 2);
            discarding_ = false;
            return true;
        }
    }
    const bool esc_last = !buf_.empty() && buf_.back() == kEsc;
    buf_.assign(esc_last ? 1 : 0, kEsc);
    return false;
}

InputDecoder::Parse InputDecoder::parse_csi(std::vector<InputEvent>& out) {
    // Linux console function keys: ESC [ [ A..E.
    if (buf_.size() >= 3 && buf_[2] == '[') {
        if (buf_.size() < 4) return Parse::need_more;
        const char c = buf_[3];
        if (c >= 'A' && c <= 'E') out.emplace_back(key(static_cast<Key>(static_cast<int>(Key::F1) + (c - 'A'))));
        buf_.erase(0, 4);
        return Parse::done;
    }
    std::size_t i = 2;
    while (i < buf_.size()) {
        const auto c = static_cast<unsigned char>(buf_[i]);
        if (c >= 0x40 && c <= 0x7E) break;
        if (c == '$' && i > 2) break;  // rxvt: ESC [ n $ is Shift + the ESC [ n ~ key
        if (c < 0x20 || i >= kMaxSequence) {
            // Not a sequence after all: drop what was read.
            buf_.erase(0, i);
            return Parse::done;
        }
        ++i;
    }
    if (i >= buf_.size()) return Parse::need_more;
    const char final_byte = buf_[i];
    const std::string_view body = std::string_view(buf_).substr(2, i - 2);
    const bool private_marker = !body.empty() && (body[0] == '<' || body[0] == '=' || body[0] == '>' || body[0] == '?');
    const std::vector<int> p = params_of(body);
    const std::uint8_t mods = p.size() >= 2 ? mods_from_param(p[1]) : 0;
    std::optional<KeyEvent> ev;
    bool paste = false;
    if (!private_marker) {
        if (final_byte == '$' || final_byte == '^' || final_byte == '@') {
            // rxvt: `$` Shift, `^` Ctrl, `@` Ctrl+Shift on the `~` keys.
            const std::uint8_t m = final_byte == '$' ? kShift : final_byte == '^' ? kCtrl : static_cast<std::uint8_t>(kCtrl | kShift);
            if (const auto k = tilde_key(p[0])) ev = key(*k, m);
        } else if (p.size() == 1 && body.empty() && final_byte >= 'a' && final_byte <= 'd') {
            if (const auto k = final_key(static_cast<char>(final_byte - 'a' + 'A'))) ev = key(*k, kShift);  // rxvt Shift+arrow
        } else if (final_byte == '~') {
            if (p[0] == 200) {
                paste = true;
            } else if (p[0] == 27 && p.size() >= 3) {
                ev = code_key(p[2], mods);
            } else if (const auto k = tilde_key(p[0])) {
                ev = key(*k, mods);
            }
        } else if (final_byte == 'Z') {
            ev = key(Key::BackTab, mods);
        } else if (final_byte == 'u') {
            ev = code_key(p[0], mods);
        } else if (const auto k = final_key(final_byte)) {
            ev = key(*k, mods);
        }
    }
    buf_.erase(0, i + 1);
    if (paste) {
        in_paste_ = true;
        paste_.clear();
    } else if (ev) {
        out.emplace_back(*ev);
    }
    return Parse::done;
}

InputDecoder::Parse InputDecoder::parse_ss3(std::vector<InputEvent>& out) {
    std::size_t i = 2;
    while (i < buf_.size() && buf_[i] >= '0' && buf_[i] <= '9' && i < 6) ++i;
    if (i >= buf_.size()) return Parse::need_more;
    const char final_byte = buf_[i];
    const std::uint8_t mods = i > 2 ? mods_from_param(params_of(std::string_view(buf_).substr(2, i - 2))[0]) : 0;
    if (final_byte >= 'a' && final_byte <= 'd') {
        if (const auto k = final_key(static_cast<char>(final_byte - 'a' + 'A'))) out.emplace_back(key(*k, kCtrl));  // rxvt Ctrl+arrow
    } else if (const auto k = final_key(final_byte)) {
        out.emplace_back(key(*k, mods));
    }
    buf_.erase(0, i + 1);
    return Parse::done;
}

std::optional<std::chrono::milliseconds> InputDecoder::pending_timeout() const {
    // A paste or a terminal string may arrive over many reads; only a long silence ends it.
    if (in_paste_ || discarding_ || in_string()) return kPasteIdle;
    if (buf_.empty()) return std::nullopt;
    return kEscTimeout;
}

bool InputDecoder::in_string() const { return buf_.size() >= 3 && buf_[0] == kEsc && opens_string(buf_[1], buf_[2]); }

std::vector<InputEvent> InputDecoder::timeout() {
    std::vector<InputEvent> out;
    if (in_paste_) {  // the terminator never came: the paste is what arrived
        paste_ += buf_;
        buf_.clear();
        end_paste(out);
        return out;
    }
    if (discarding_ || in_string()) {
        discarding_ = false;
        buf_.clear();
        return out;
    }
    if (buf_.empty()) return out;
    if (buf_ == "\x1b") {
        out.emplace_back(key(Key::Escape));
    } else if (buf_ == "\x1b[" || buf_ == "\x1bO") {
        out.emplace_back(key(Key::Char, kAlt, static_cast<char32_t>(buf_[1])));
    } else if (buf_[0] != kEsc) {
        // A truncated UTF-8 sequence that never completed.
        for (std::size_t i = 0; i < buf_.size(); ++i) out.emplace_back(key(Key::Char, 0, kReplacement));
    } else if (buf_.size() == 2 && (buf_[1] == ']' || buf_[1] == 'P')) {
        out.emplace_back(key(Key::Char, kAlt, static_cast<char32_t>(buf_[1])));  // Alt+] or Alt+P, not a string after all
    } else if (buf_.size() >= 2 && buf_[1] != '[' && buf_[1] != 'O') {
        out.emplace_back(key(Key::Char, kAlt, kReplacement));  // Alt + a truncated sequence
    }
    // Anything else is a truncated escape sequence, dropped.
    buf_.clear();
    return out;
}

}  // namespace mod
