#include "ui/prompt.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <span>
#include <utility>

#include "text/utf8.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {

// Byte length of the code point that starts at `i` / ends at `i`.
std::size_t next_cp(std::string_view s, std::size_t i) {
    if (i >= s.size()) return 0;
    const Decoded d = decode(std::as_bytes(std::span(s.data() + i, s.size() - i)));
    return std::max<std::size_t>(1, d.len);
}

std::size_t prev_cp(std::string_view s, std::size_t i) {
    if (i == 0) return 0;
    const Decoded d = decode_before(std::as_bytes(std::span(s.substr(0, i))));
    return std::max<std::size_t>(1, d.len);
}


std::vector<std::string> split_lines(std::string_view text) {
    std::vector<std::string> out;
    std::size_t start = 0;
    for (;;) {
        const std::size_t nl = text.find('\n', start);
        out.emplace_back(text.substr(start, nl == std::string_view::npos ? std::string_view::npos : nl - start));
        if (nl == std::string_view::npos) break;
        start = nl + 1;
    }
    return out;
}

}  // namespace

Prompt::Prompt(Searcher* searcher) : searcher_(searcher) {}

void Prompt::reset(PromptKind kind) {
    if (kind_ == PromptKind::find || kind_ == PromptKind::replace) last_query_ = fields_[0].text;
    open_ = true;
    ++generation_;
    kind_ = kind;
    label_.clear();
    for (Field& f : fields_) f = Field{};
    active_ = 0;
    error_.clear();
    on_submit_ = nullptr;
    cursor_ = nullptr;
    lines_.clear();
    scroll_ = 0;
}

void Prompt::open(PromptKind kind, std::string label, std::string initial_text, SubmitFn on_submit) {
    reset(kind);
    label_ = std::move(label);
    if (const auto nl = initial_text.find('\n'); nl != std::string::npos) initial_text.resize(nl);
    fields_[0].text = std::move(initial_text);
    fields_[0].cursor = fields_[0].text.size();
    on_submit_ = std::move(on_submit);
}

void Prompt::open_find(std::string initial_text, std::uint64_t origin, CursorFn cursor, bool allow_replace) {
    const SearchOptions keep = options_;
    reset(PromptKind::find);
    options_ = keep;
    label_ = "Find:";
    if (const auto nl = initial_text.find('\n'); nl != std::string::npos) initial_text.resize(nl);
    fields_[0].text = std::move(initial_text);
    fields_[0].cursor = fields_[0].text.size();
    origin_ = origin;
    cursor_ = std::move(cursor);
    allow_replace_ = allow_replace;
    if (!fields_[0].text.empty()) search_changed();
}

void Prompt::open_info(std::string title, std::string_view text) {
    reset(PromptKind::info);
    label_ = std::move(title);
    lines_ = split_lines(text);
}

void Prompt::set_searcher(Searcher* searcher) {
    if (open_ && (kind_ == PromptKind::find || kind_ == PromptKind::replace)) close();
    searcher_ = searcher;
}

void Prompt::close() {
    if (open_ && (kind_ == PromptKind::find || kind_ == PromptKind::replace) && searcher_) searcher_->cancel();
    open_ = false;
    on_submit_ = nullptr;
    cursor_ = nullptr;
}

void Prompt::insert(Field& f, std::string_view bytes) {
    f.text.insert(f.cursor, bytes);
    f.cursor += bytes.size();
}

bool Prompt::edit_field(Field& f, const KeyEvent& key) {
    switch (key.key) {
        case Key::Left: f.cursor -= prev_cp(f.text, f.cursor); return true;
        case Key::Right: f.cursor += next_cp(f.text, f.cursor); return true;
        case Key::Home: f.cursor = 0; return true;
        case Key::End: f.cursor = f.text.size(); return true;
        case Key::Backspace: {
            const std::size_t n = prev_cp(f.text, f.cursor);
            f.text.erase(f.cursor - n, n);
            f.cursor -= n;
            return true;
        }
        case Key::Delete: f.text.erase(f.cursor, next_cp(f.text, f.cursor)); return true;
        case Key::CtrlLetter:
            if (key.ch == U'a') f.cursor = 0;
            if (key.ch == U'e') f.cursor = f.text.size();
            return key.ch == U'a' || key.ch == U'e';
        case Key::Char:
            if (key.mods & (kAlt | kCtrl)) return false;
            insert(f, to_utf8(key.ch));
            return true;
        default: return false;
    }
}

void Prompt::search_changed() {
    error_.clear();
    if (searcher_ == nullptr) return;
    const std::string& text = fields_[0].text;
    if (text.empty()) {
        searcher_->cancel();
        return;
    }
    if (auto s = searcher_->set_query(text, options_); !s) {
        error_ = s.error().message;
        return;
    }
    searcher_->find_next(origin_, Direction::forward);
}

void Prompt::submit() {
    // The callback may open the prompt again; only an unchanged prompt is reopened with the error.
    SubmitFn cb = std::move(on_submit_);
    const std::uint64_t gen = generation_;
    const PromptKind kind = kind_;
    const std::string label = label_;
    const Field field = fields_[0];
    open_ = false;
    std::optional<std::string> err = cb ? cb(field.text) : std::nullopt;
    if (err && !open_ && generation_ == gen) {
        open_ = true;
        kind_ = kind;
        label_ = label;
        fields_[0] = field;
        error_ = std::move(*err);
        on_submit_ = std::move(cb);
    }
}

Prompt::Outcome Prompt::handle_event(const InputEvent& event) {
    if (!open_) return Outcome::closed;
    if (const auto* paste = std::get_if<PasteEvent>(&event)) {
        if (kind_ == PromptKind::info) return Outcome::consumed;
        std::string_view bytes = paste->bytes;
        if (const auto nl = bytes.find_first_of("\r\n"); nl != std::string_view::npos) bytes = bytes.substr(0, nl);
        insert(fields_[active_], bytes);
        if ((kind_ == PromptKind::find || kind_ == PromptKind::replace) && active_ == 0) search_changed();
        return Outcome::consumed;
    }
    return handle_key(std::get<KeyEvent>(event));
}

Prompt::Outcome Prompt::handle_key(const KeyEvent& key) {
    switch (kind_) {
        case PromptKind::find:
        case PromptKind::replace: return handle_find_key(key);
        case PromptKind::info: {
            const int max_scroll = std::max(0, static_cast<int>(lines_.size()) - 1);
            switch (key.key) {
                case Key::Escape:
                case Key::Enter: close(); return Outcome::closed;
                case Key::Up: scroll_ = std::max(0, scroll_ - 1); break;
                case Key::Down: scroll_ = std::min(max_scroll, scroll_ + 1); break;
                case Key::PageUp: scroll_ = std::max(0, scroll_ - 10); break;
                case Key::PageDown: scroll_ = std::min(max_scroll, scroll_ + 10); break;
                case Key::Home: scroll_ = 0; break;
                case Key::End: scroll_ = max_scroll; break;
                case Key::Char:
                    if (key.ch == U'q' && key.mods == 0) {
                        close();
                        return Outcome::closed;
                    }
                    break;
                default: break;
            }
            return Outcome::consumed;
        }
        default: break;
    }
    // A single-field prompt.
    if (key.key == Key::Escape) {
        close();
        return Outcome::closed;
    }
    if (key.key == Key::Enter) {
        submit();
        return open_ ? Outcome::consumed : Outcome::closed;
    }
    if (edit_field(fields_[0], key)) error_.clear();
    return Outcome::consumed;
}

Prompt::Outcome Prompt::handle_find_key(const KeyEvent& key) {
    if (key.key == Key::Escape) {
        close();
        return Outcome::closed;
    }
    if (key.key == Key::Tab || key.key == Key::BackTab) {
        if (!allow_replace_) return Outcome::consumed;  // a read-only view: find only
        if (kind_ == PromptKind::find) {
            kind_ = PromptKind::replace;
            active_ = 1;
        } else {
            active_ = 1 - active_;
        }
        return Outcome::consumed;
    }
    if (key.key == Key::Char && (key.mods & kAlt) && !(key.mods & kCtrl)) {
        switch (std::tolower(static_cast<int>(key.ch < 0x80 ? key.ch : 0))) {
            case 'r': options_.regex = !options_.regex; search_changed(); break;
            case 'c': options_.case_insensitive = !options_.case_insensitive; search_changed(); break;
            case 'w': options_.whole_word = !options_.whole_word; search_changed(); break;
            case 'a':
                if (kind_ == PromptKind::replace && searcher_ && searcher_->has_query()) {
                    if (auto s = searcher_->replace_all(fields_[1].text); !s) error_ = s.error().message;
                }
                break;
            default: break;
        }
        return Outcome::consumed;
    }
    if (key.key == Key::Enter) {
        if (searcher_ == nullptr || !searcher_->has_query()) return Outcome::consumed;
        const std::uint64_t from = cursor_ ? cursor_() : origin_;
        if (kind_ == PromptKind::replace && !(key.mods & kShift)) {
            if (auto s = searcher_->replace_current(fields_[1].text); !s) {
                // Not on a match yet: find one first.
                if (s.error().code == ErrorCode::canceled) {
                    searcher_->find_next(from, Direction::forward);
                } else {
                    error_ = s.error().message;
                }
            }
            return Outcome::consumed;
        }
        searcher_->find_next(from, (key.mods & kShift) ? Direction::backward : Direction::forward);
        return Outcome::consumed;
    }
    Field& f = fields_[active_];
    const std::string before = f.text;
    if (edit_field(f, key) && active_ == 0 && f.text != before) search_changed();
    return Outcome::consumed;
}

int Prompt::rows_wanted(int screen_rows, int /*screen_cols*/) const {
    if (!open_) return 0;
    switch (kind_) {
        case PromptKind::replace: return 2;
        case PromptKind::info: return std::max(1, std::min(static_cast<int>(lines_.size()) + 1, screen_rows - 3));
        default: return 1;
    }
}

void Prompt::render(Screen& screen, Rect area) const {
    if (!open_ || area.rows <= 0) return;
    const Attr bar = attr_for(Style::status);
    const Attr err = attr_for(Style::error);
    const int right = area.col + area.cols;
    for (int r = area.row; r < area.row + area.rows; ++r) screen.fill(r, area.col, right, bar);

    if (kind_ == PromptKind::info) {
        int col = screen.print(area.row, area.col + 1, right, label_, Attr{bar.fg, bar.bg, static_cast<std::uint8_t>(bar.flags | kBold)});
        screen.print(area.row, col, right, "  (Esc to close, arrows to scroll)", bar);
        for (int r = 1; r < area.rows; ++r) {
            const auto i = static_cast<std::size_t>(scroll_ + r - 1);
            if (i < lines_.size()) screen.print(area.row + r, area.col + 1, right, lines_[i], bar);
        }
        screen.set_cursor(0, 0, false);
        return;
    }

    auto draw_field = [&](int row, std::string_view label, const Field& f, bool active) {
        int col = screen.print(row, area.col + 1, right, label, bar);
        col = screen.print(row, col, right, " ", bar);
        const int field_start = col;
        // Scroll the field so the cursor stays visible.
        const int avail = std::max(1, right - field_start - 1);
        const int cursor_col = text_columns(std::string_view(f.text).substr(0, f.cursor));
        std::size_t from = 0;
        int skipped = 0;
        while (cursor_col - skipped >= avail && from < f.text.size()) {
            const std::size_t n = next_cp(f.text, from);
            skipped += text_columns(std::string_view(f.text).substr(from, n));
            from += n;
        }
        col = screen.print(row, col, right, std::string_view(f.text).substr(from), bar);
        if (active) screen.set_cursor(row, field_start + cursor_col - skipped, true);
        return col;
    };

    if (kind_ == PromptKind::find || kind_ == PromptKind::replace) {
        const std::string flags = std::format("[{}regex {}case {}word]", options_.regex ? "x" : " ",
                                              options_.case_insensitive ? "i" : " ", options_.whole_word ? "w" : " ");
        int col = draw_field(area.row, "Find:", fields_[0], active_ == 0);
        const int flags_col = right - static_cast<int>(flags.size()) - 1;
        if (!error_.empty()) {
            screen.print(area.row, std::min(col + 2, flags_col), flags_col, error_, err);
        }
        if (flags_col > col) screen.print(area.row, flags_col, right, flags, bar);
        if (kind_ == PromptKind::replace && area.rows >= 2) {
            draw_field(area.row + 1, "Replace:", fields_[1], active_ == 1);
            screen.print(area.row + 1, std::max(area.col, right - 28), right, "Enter: replace  Alt+A: all", bar);
        }
        return;
    }
    const int col = draw_field(area.row, label_, fields_[0], true);
    if (!error_.empty()) screen.print(area.row, col + 2, right, error_, err);
}

}  // namespace mod
