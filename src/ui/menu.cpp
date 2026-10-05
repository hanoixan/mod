#include "ui/menu.hpp"

#include <algorithm>
#include <cassert>
#include <cctype>
#include <format>
#include <set>

#include "text/utf8.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {

using C = CommandId;

constexpr auto kDocumentsMenu = static_cast<std::size_t>(MenuId::documents);
constexpr auto kOptionsMenu = static_cast<std::size_t>(MenuId::options);

std::vector<Menu> menu_table() {
    return {
        {"File", 'f',
         {{"Open…", 'o', C::Open, false},
          {"Save", 's', C::Save, false},
          {"Save As…", 'a', C::SaveAs, false},
          {"Close", 'c', C::CloseDocument, false},
          {"Suspend", 'u', C::Suspend, false},
          {"Exit", 'x', C::Exit, false}}},
        {"Edit", 'e',
         {{"Undo", 'u', C::Undo, false},
          {"Redo", 'r', C::Redo, false},
          {"Undo History…", 'h', C::UndoHistory, false},
          {"Next Branch", 'b', C::NextBranch, false},
          {"Previous Branch", 'v', C::PrevBranch, false},
          {"Cut", 't', C::Cut, false},
          {"Cut to Line End", 'l', C::CutToLineEnd, false},
          {"Copy", 'c', C::Copy, false},
          {"Paste", 'p', C::Paste, false},
          {"Find/Replace", 'f', C::Find, false},
          {"Find Next", 'n', C::FindNext, false},
          {"Find Previous", 'i', C::FindPrev, false},
          {"Go to Line…", 'g', C::GotoLine, false}}},
        {"View", 'v',
         {{"Line Numbers", 'l', C::ToggleLineNumbers, true},
          {"Syntax Coloring", 's', C::ToggleSyntax, true},
          {"Word Wrap", 'w', C::ToggleWordWrap, true},
          {"Read Only", 'r', C::ToggleReadOnly, true},
          {"Pin Folder Tree", 'f', C::PinFolderTree, true}, {}, {"Split", 'p', C::Split, false}, {"Unsplit", 'u', C::Unsplit, false}}},
        {"Documents", 'd', {}},
        {"Options", 'o', {{"User Settings…", 'u', C::UserSettings, false}, {}, {"Key Bindings…", 'k', C::KeyBindings, false}, {"Colors…", 'l', C::Colors, false}}},
        {"Help", 'h', {{"Documentation", 'd', C::ShowHelp, false}, {"About mod", 'a', C::About, false}}},
    };
}


// Byte offset of the first occurrence of `accel` (either case) in `label`.
std::size_t accel_pos(std::string_view label, char accel) {
    for (std::size_t i = 0; i < label.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(label[i])) == accel) return i;
    }
    return std::string_view::npos;
}

// Draws `label` with its accelerator letter underlined.
int draw_label(Screen& screen, int row, int col, int col_end, std::string_view label, char accel, Attr attr) {
    const std::size_t at = accel_pos(label, accel);
    if (at == std::string_view::npos) return screen.print(row, col, col_end, label, attr);
    col = screen.print(row, col, col_end, label.substr(0, at), attr);
    Attr underlined = attr;
    underlined.flags |= kUnderline;
    col = screen.print(row, col, col_end, label.substr(at, 1), underlined);
    return screen.print(row, col, col_end, label.substr(at + 1), attr);
}

}  // namespace

MenuBar::MenuBar(const Keymap& keymap) : keymap_(keymap), menus_(menu_table()) {
#ifndef NDEBUG
    // Two items of one menu must not share an accelerator, and each must appear in its label.
    for (const Menu& m : menus_) {
        std::set<char> seen;
        for (const MenuItem& item : m.items) {
            if (item.label.empty()) continue;
            assert(seen.insert(item.accel).second);
            assert(accel_pos(item.label, item.accel) != std::string_view::npos);
        }
    }
#endif
}

std::string help_hint(const Keymap& /*keymap*/) { return "Esc h: help"; }  // Esc always opens the menu

void MenuBar::show() {
    visible_ = true;
    open_ = false;
    flashing_ = false;
    menu_ = 0;
    item_ = 0;
}

void MenuBar::open(MenuId menu) { open_at(static_cast<int>(menu)); }

void MenuBar::open_at(int menu_index) {
    visible_ = true;
    open_ = true;
    flashing_ = false;
    menu_ = std::clamp(menu_index, 0, static_cast<int>(menus_.size()) - 1);
    item_ = 0;
}

void MenuBar::set_documents(const std::vector<DocumentMenuEntry>& documents) {
    Menu& menu = menus_[kDocumentsMenu];
    menu.items.clear();
    for (std::size_t i = 0; i < documents.size(); ++i) {
        const DocumentMenuEntry& d = documents[i];
        const bool numbered = i < 9;
        std::string label = numbered ? std::format("{} {}", i + 1, d.label) : d.label;
        if (d.dirty) label += " *";
        MenuItem item{std::move(label), numbered ? static_cast<char>('1' + i) : '\0', C::ShowDocument, true};
        item.arg = static_cast<int>(i);
        item.checked = d.shown;
        menu.items.push_back(std::move(item));
    }
    if (menu_ == static_cast<int>(kDocumentsMenu)) item_ = std::clamp(item_, 0, std::max(0, static_cast<int>(menu.items.size()) - 1));
}

void MenuBar::set_recent_settings(const Settings& settings) {
    Menu& options = menus_[kOptionsMenu];
    options.items.assign(1, MenuItem{"User Settings…", 'u', C::UserSettings, false});
    std::size_t n = 0;
    for (const SettingSpec* spec : settings.recent(kRecentSettingCount)) {
        const bool boolean = spec->type == SettingType::boolean;
        // An on/off setting shows its state as a check mark; a number shows its value.
        std::string label = boolean ? std::format("{} {}", n + 1, spec->label)
                                    : spec->type == SettingType::choice
                                          ? std::format("{} {}: {}", n + 1, spec->label, setting_value_text(*spec, settings.value(spec->key)))
                                          : std::format("{} {}: {}…", n + 1, spec->label, settings.value(spec->key));
        options.items.push_back(MenuItem{std::move(label), static_cast<char>('1' + n), recent_setting_command(n), boolean});
        ++n;
    }
    options.items.push_back(MenuItem{});  // a separator
    options.items.push_back(MenuItem{"Key Bindings…", 'k', C::KeyBindings, false});
    options.items.push_back(MenuItem{"Colors…", 'l', C::Colors, false});
    if (menu_ == static_cast<int>(kOptionsMenu)) item_ = std::min(item_, static_cast<int>(options.items.size()) - 1);
}

void MenuBar::move_item(int delta) {
    const auto& items = menus_[static_cast<std::size_t>(menu_)].items;
    const int n = static_cast<int>(items.size());
    if (n == 0) return;
    for (int step = 0; step < n; ++step) {
        item_ = (item_ + delta + n) % n;
        if (!items[static_cast<std::size_t>(item_)].label.empty()) return;
    }
}

std::optional<CommandId> MenuBar::handle_key(const KeyEvent& key) {
    if (!visible_ || flashing_) return std::nullopt;  // keys wait while the chosen item flashes
    const int n = static_cast<int>(menus_.size());
    if (!open_) {
        // Armed: the next key picks a menu.
        switch (key.key) {
            case Key::Escape: hide(); break;
            case Key::Left: menu_ = (menu_ + n - 1) % n; break;
            case Key::Right: menu_ = (menu_ + 1) % n; break;
            case Key::Enter:
            case Key::Up:
            case Key::Down: open_at(menu_); break;
            case Key::Char: {
                if (key.ch >= 0x80 || (key.mods & (kAlt | kCtrl))) break;
                const char c = static_cast<char>(std::tolower(static_cast<int>(key.ch)));
                for (int i = 0; i < n; ++i) {
                    if (menus_[static_cast<std::size_t>(i)].accel == c) {
                        open_at(i);
                        break;
                    }
                }
                break;
            }
            default: break;  // consumed
        }
        return std::nullopt;
    }
    const auto& items = menus_[static_cast<std::size_t>(menu_)].items;
    switch (key.key) {
        case Key::Escape: hide(); return std::nullopt;
        case Key::Left: open_at((menu_ + n - 1) % n); return std::nullopt;
        case Key::Right: open_at((menu_ + 1) % n); return std::nullopt;
        case Key::Up: move_item(-1); return std::nullopt;
        case Key::Down: move_item(1); return std::nullopt;
        case Key::Home: item_ = 0; return std::nullopt;
        case Key::End: item_ = std::max(0, static_cast<int>(items.size()) - 1); return std::nullopt;  // never a separator
        case Key::Enter: {
            if (items.empty()) return std::nullopt;
            close();
            chosen_arg_ = items[static_cast<std::size_t>(item_)].arg;
            return items[static_cast<std::size_t>(item_)].command;
        }
        case Key::Char: {
            if (key.ch >= 0x80 || (key.mods & (kAlt | kCtrl))) return std::nullopt;
            const char c = static_cast<char>(std::tolower(static_cast<int>(key.ch)));
            for (std::size_t k = 0; k < items.size(); ++k) {
                const MenuItem& item = items[k];
                if (!item.label.empty() && item.accel != 0 && item.accel == c) {
                    // Shown chosen for a moment; App runs it when the flash ends, then hides the bar.
                    item_ = static_cast<int>(k);
                    flashing_ = true;
                    chosen_arg_ = item.arg;
                    return item.command;
                }
            }
            return std::nullopt;
        }
        default: return std::nullopt;  // consumed
    }
}

void MenuBar::render(Screen& screen, int bar_row, const std::function<bool(CommandId)>& checked) const {
    if (!visible_ || bar_row < 0 || bar_row >= screen.rows()) return;  // hidden: the rows are the text's
    const Attr bar = attr_for(Style::menu);
    const Attr selected = attr_for(Style::menu_selected);
    screen.fill(bar_row, 0, screen.cols(), bar);
    int col = 1;
    std::vector<int> starts;
    for (std::size_t i = 0; i < menus_.size(); ++i) {
        starts.push_back(col);
        const bool active = static_cast<int>(i) == menu_;  // the open menu, or the armed highlight
        const Attr a = active ? selected : bar;
        col = screen.print(bar_row, col, screen.cols(), " ", a);
        col = draw_label(screen, bar_row, col, screen.cols(), menus_[i].title, menus_[i].accel, a);
        col = screen.print(bar_row, col, screen.cols(), " ", a);
        col += 1;
    }
    if (!open_) return;

    const Menu& m = menus_[static_cast<std::size_t>(menu_)];
    int label_w = 0;
    int key_w = 0;
    for (const MenuItem& item : m.items) {
        if (item.label.empty()) continue;  // a separator
        label_w = std::max(label_w, text_columns(item.label));
        key_w = std::max(key_w, text_columns(keymap_.binding_label(item.command)));
    }
    const int width = 2 + 2 + label_w + (key_w > 0 ? 2 + key_w : 0) + 1;  // check column, label, keys
    int left = starts[static_cast<std::size_t>(menu_)];
    left = std::max(0, std::min(left, screen.cols() - width));
    const int right = std::min(screen.cols(), left + width);
    // The menu opens downward from the bar; rows below the screen are cut.
    int row = bar_row + 1;
    for (std::size_t i = 0; i < m.items.size() && row < screen.rows(); ++i, ++row) {
        const MenuItem& item = m.items[i];
        const Attr a = static_cast<int>(i) == item_ ? selected : bar;
        screen.fill(row, left, right, a);
        if (item.label.empty()) {
            for (int c = left + 1; c < right - 1; ++c) screen.put(row, c, "─", 1, a);
            continue;
        }
        const bool is_checked = item.checked ? *item.checked : (checked && checked(item.command));
        if (item.checkable && is_checked) screen.print(row, left + 1, right, "✓", a);
        draw_label(screen, row, left + 3, right, item.label, item.accel, a);
        const std::string keys = keymap_.binding_label(item.command);
        if (!keys.empty()) screen.print(row, right - 1 - text_columns(keys), right, keys, a);
    }
}

}  // namespace mod
