#include "ui/keymap_view.hpp"

#include <algorithm>
#include <cctype>
#include <format>
#include <span>

#include "text/utf8.hpp"
#include "ui/list_cursor.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {

constexpr int kKeysGap = 3;  // columns between the longest name and the keys


std::string lower(std::string_view s) {
    std::string out(s);
    for (char& c : out) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

int name_width() {
    int w = 0;
    for (const CommandInfo& info : all_commands()) {
        if (info.bindable) w = std::max(w, text_columns(command_display_name(info.id)));
    }
    return w;
}

std::string keys_text(const Keymap& keymap, CommandId command) {
    std::string out;
    for (const KeyEvent& key : keymap.keys_for(command)) {
        if (!out.empty()) out += ", ";
        out += Keymap::key_label(key);
    }
    return out.empty() ? std::string("(none)") : out;
}

}  // namespace

void KeymapView::open(Keymap& keymap) {
    keymap_ = &keymap;
    search_.clear();
    selected_ = 0;
    scroll_ = 0;
    capturing_ = false;
    filter();
}

void KeymapView::close() {
    keymap_ = nullptr;
    capturing_ = false;
    rows_.clear();
}

std::optional<CommandId> KeymapView::selected_command() const {
    if (selected_ >= rows_.size()) return std::nullopt;
    return rows_[selected_];
}

std::string KeymapView::row_text(std::size_t index) const {
    if (keymap_ == nullptr || index >= rows_.size()) return {};
    const CommandId command = rows_[index];
    std::string out = command_display_name(command);
    const int pad = name_width() - text_columns(out) + kKeysGap;
    out.append(static_cast<std::size_t>(pad), ' ');
    out += keys_text(*keymap_, command);
    if (!keymap_->is_default(command)) out += " *";
    return out;
}

// A command matches when every word of the search is found in its display name, its
// internal name or its keys, in any case.
void KeymapView::filter() {
    rows_.clear();
    selected_ = 0;
    scroll_ = 0;
    if (keymap_ == nullptr) return;
    std::vector<std::string> words;
    std::string word;
    for (const char c : lower(search_)) {
        if (c == ' ') {
            if (!word.empty()) words.push_back(std::move(word));
            word.clear();
        } else {
            word += c;
        }
    }
    if (!word.empty()) words.push_back(std::move(word));
    for (const CommandInfo& info : all_commands()) {
        if (!info.bindable) continue;
        const std::string haystack =
            lower(command_display_name(info.id)) + '\n' + lower(info.name) + '\n' + lower(keys_text(*keymap_, info.id));
        const bool match = std::all_of(words.begin(), words.end(), [&](const std::string& w) { return haystack.find(w) != std::string::npos; });
        if (match) rows_.push_back(info.id);
    }
}

void KeymapView::handle_paste(std::string_view bytes) {
    if (keymap_ == nullptr || capturing_) return;
    if (const auto nl = bytes.find_first_of("\r\n"); nl != std::string_view::npos) bytes = bytes.substr(0, nl);
    search_ += bytes;
    filter();
}

void KeymapView::reset_all() {
    if (keymap_ == nullptr) return;
    keymap_->reset_all();
}

KeymapKeyResult KeymapView::capture(const KeyEvent& key) {
    KeymapKeyResult r;
    capturing_ = false;
    if (key.key == Key::Escape) return r;  // canceled
    const auto command = selected_command();
    if (!command) return r;
    const std::string label = Keymap::key_label(Keymap::normalized(key));
    const std::string name = command_display_name(*command);
    const BindOutcome outcome = keymap_->bind(*command, key);
    switch (outcome.kind) {
        case BindOutcome::bound:
            r.changed = true;
            r.message = std::format("{} added to {}", label, name);
            break;
        case BindOutcome::already_bound: r.message = std::format("{} is already bound to {}", label, name); break;
        case BindOutcome::taken:
            r.message = std::format("{} is bound to {}; remove it there first", label, command_display_name(*outcome.owner));
            break;
        case BindOutcome::not_bindable:
            r.message = key.key == Key::Char ? std::format("{} types text; it cannot be bound", label) : std::format("{} cannot be bound", label);
            break;
    }
    return r;
}

KeymapKeyResult KeymapView::handle_key(const KeyEvent& key) {
    KeymapKeyResult r;
    if (keymap_ == nullptr) return r;
    if (capturing_) return capture(key);
    const auto command = selected_command();
    switch (key.key) {
        case Key::Escape:
            close();
            r.closed = true;
            break;
        case Key::Up:
        case Key::Down:
        case Key::Home:
        case Key::End:
        case Key::PageUp:
        case Key::PageDown:
            if (const auto to = list_step(key.key, selected_, rows_.size())) selected_ = to->index;
            break;
        case Key::Enter:
            if (command) {
                capturing_ = true;
            } else {
                r.message = "no command selected";
            }
            break;
        case Key::Delete: {
            if (!command) break;
            const std::vector<KeyEvent> keys = keymap_->keys_for(*command);
            if (keys.empty()) {
                r.message = std::format("{} has no key to remove", command_display_name(*command));
                break;
            }
            keymap_->unbind(*command, keys.back());
            r.changed = true;
            r.message = std::format("{} removed from {}", Keymap::key_label(keys.back()), command_display_name(*command));
            break;
        }
        case Key::Backspace:
            if (key.mods & kCtrl) {
                search_.clear();
            } else if (!search_.empty()) {
                const Decoded d = decode_before(std::as_bytes(std::span(search_.data(), search_.size())));
                search_.resize(search_.size() - std::max<std::size_t>(1, d.len));
            }
            filter();
            break;
        case Key::CtrlLetter: {
            if (key.ch != U'r' || !command) break;
            const std::string name = command_display_name(*command);
            if (keymap_->is_default(*command)) {
                r.message = std::format("{} already has its default keys", name);
                break;
            }
            const std::vector<KeyEvent> elsewhere = keymap_->reset(*command);
            r.changed = true;
            // Refusing to take a key applies here too: it stays where the user put it.
            const auto owner = elsewhere.empty() ? std::nullopt : keymap_->owner(elsewhere.front());
            if (!owner) {
                r.message = std::format("{} reset to its default keys", name);
            } else {
                r.message = std::format("{} reset; {} stays with {}", name, Keymap::key_label(elsewhere.front()),
                                        command_display_name(*owner));
            }
            break;
        }
        case Key::Char:
            if ((key.mods & kAlt) && !(key.mods & kCtrl) && (key.ch == U'r' || key.ch == U'R')) {
                r.reset_all = true;
            } else if ((key.mods & (kAlt | kCtrl)) == 0) {
                search_ += to_utf8(key.ch);
                filter();
            }
            break;
        default: break;  // consumed
    }
    return r;
}

void KeymapView::render(Screen& screen, Rect area) {
    if (keymap_ == nullptr || area.rows <= 0 || area.cols <= 0) return;
    const Attr plain = attr_for(Style::Default);
    const Attr selected = attr_for(Style::list_selected);
    const int right = area.col + area.cols;
    for (int r = 0; r < area.rows; ++r) screen.fill(area.row + r, area.col, right, plain);

    // The first row is the search field, or the capture request while a key is awaited.
    if (capturing_) {
        const auto command = selected_command();
        const std::string name = command ? command_display_name(*command) : std::string();
        screen.print(area.row, area.col + 1, right, std::format("Press the new key for {}   (Esc cancels)", name),
                     attr_for(Style::gutter_current));
        screen.set_cursor(0, 0, false);
    } else {
        int col = screen.print(area.row, area.col + 1, right, "Search: ", attr_for(Style::gutter_current));
        col = screen.print(area.row, col, right, search_, plain);
        screen.set_cursor(area.row, std::min(col, right - 1), true);
    }

    const int list_rows = area.rows - 1;
    if (list_rows <= 0) return;
    const auto visible = static_cast<std::size_t>(list_rows);
    scroll_ = scroll_to_show(selected_, scroll_, visible);
    if (rows_.empty()) {
        screen.print(area.row + 1, area.col + 2, right, "no command matches", attr_for(Style::gutter));
        return;
    }
    for (int r = 0; r < list_rows; ++r) {
        const std::size_t i = scroll_ + static_cast<std::size_t>(r);
        if (i >= rows_.size()) break;
        const int row = area.row + 1 + r;
        const Attr a = i == selected_ ? selected : plain;
        screen.fill(row, area.col, right, a);
        if (i == selected_) screen.print(row, area.col, right, ">", a);
        screen.print(row, area.col + 2, right, row_text(i), a);
    }
}

}  // namespace mod
