#include "ui/text_field.hpp"

#include <algorithm>
#include <span>

#include "text/utf8.hpp"

namespace mod {
namespace {

std::span<const std::byte> bytes_of(std::string_view s) { return std::as_bytes(std::span(s.data(), s.size())); }

}  // namespace

TextField::TextField(std::string initial) : text_(std::move(initial)), cursor_(text_.size()) {}

void TextField::set_text(std::string text) {
    text_ = std::move(text);
    cursor_ = text_.size();
    scroll_ = 0;
}

std::size_t TextField::next_cluster(std::size_t pos) const {
    if (pos >= text_.size()) return text_.size();
    const auto r = next_grapheme_boundary(bytes_of(std::string_view(text_).substr(pos)), true);
    return pos + std::max<std::size_t>(1, r.length);
}

std::size_t TextField::prev_cluster(std::size_t pos) const {
    if (pos == 0) return 0;
    const auto r = prev_grapheme_boundary(bytes_of(std::string_view(text_).substr(0, pos)), true);
    return pos - std::clamp<std::size_t>(r.length, 1, pos);
}

bool TextField::handle_key(const KeyEvent& key) {
    if (key.mods & (kCtrl | kAlt)) return false;
    switch (key.key) {
        case Key::Char: insert(to_utf8(key.ch)); return true;
        case Key::Backspace:
            if (cursor_ == 0) return true;
            {
                const std::size_t start = prev_cluster(cursor_);
                text_.erase(start, cursor_ - start);
                cursor_ = start;
            }
            return true;
        case Key::Delete:
            text_.erase(cursor_, next_cluster(cursor_) - cursor_);
            return true;
        case Key::Left:
            if (cursor_ == 0) return false;
            cursor_ = prev_cluster(cursor_);
            return true;
        case Key::Right:
            if (cursor_ == text_.size()) return false;
            cursor_ = next_cluster(cursor_);
            return true;
        case Key::Home: cursor_ = 0; return true;
        case Key::End: cursor_ = text_.size(); return true;
        default: return false;
    }
}

void TextField::insert(std::string_view bytes) {
    std::string clean;
    clean.reserve(bytes.size());
    for (const char c : bytes)
        if (c != '\n' && c != '\r') clean += c;
    text_.insert(cursor_, clean);
    cursor_ += clean.size();
}

void TextField::render(Screen& screen, int row, int col, int width, Attr attr, bool focused) {
    if (width <= 0) return;
    screen.fill(row, col, col + width, attr);
    // The display column of the cursor, to scroll it into view.
    int cursor_col = 0;
    int text_cols = 0;
    for (std::size_t i = 0; i < text_.size();) {
        const Decoded d = decode(bytes_of(std::string_view(text_).substr(i)));
        const int w = std::max(0, display_width(d, static_cast<std::uint64_t>(text_cols), 1));
        if (i == cursor_) cursor_col = text_cols;
        text_cols += w;
        i += std::max<std::size_t>(1, d.len);
    }
    if (cursor_ == text_.size()) cursor_col = text_cols;
    if (cursor_col < scroll_) scroll_ = cursor_col;
    if (cursor_col >= scroll_ + width) scroll_ = cursor_col - width + 1;
    // Draw the part from the scroll offset.
    int c = 0;
    for (std::size_t i = 0; i < text_.size();) {
        const Decoded d = decode(bytes_of(std::string_view(text_).substr(i)));
        const std::size_t len = std::max<std::size_t>(1, d.len);
        const int w = std::max(0, display_width(d, static_cast<std::uint64_t>(c), 1));
        if (c >= scroll_ && c + w <= scroll_ + width && w > 0) screen.put(row, col + c - scroll_, std::string_view(text_).substr(i, len), w, attr);
        c += w;
        i += len;
    }
    if (focused) screen.set_cursor(row, col + cursor_col - scroll_, true);
}

}  // namespace mod
