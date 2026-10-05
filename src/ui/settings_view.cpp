#include "ui/settings_view.hpp"

#include <algorithm>
#include <format>
#include <string_view>
#include <vector>

#include "text/utf8.hpp"
#include "ui/list_cursor.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {

constexpr int kValueGap = 3;  // columns between the longest label and the values


int label_width() {
    int w = 0;
    for (const SettingSpec& s : setting_specs()) w = std::max(w, text_columns(s.label));
    return w;
}

// Word-wrapped at spaces; a word longer than a row is cut by the row's end.
std::vector<std::string> wrap(std::string_view text, int width) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t i = 0;
    while (i < text.size()) {
        std::size_t j = text.find(' ', i);
        if (j == std::string_view::npos) j = text.size();
        const std::string_view word = text.substr(i, j - i);
        if (!line.empty() && text_columns(line) + 1 + text_columns(word) > width) {
            lines.push_back(std::move(line));
            line.clear();
        }
        if (!line.empty()) line += ' ';
        line += word;
        i = j + 1;
    }
    if (!line.empty()) lines.push_back(std::move(line));
    return lines;
}

}  // namespace

void SettingsView::open(const Settings& settings) {
    settings_ = &settings;
    selected_ = 0;
    scroll_ = 0;
}

void SettingsView::close() { settings_ = nullptr; }

std::size_t SettingsView::row_count() const { return setting_specs().size(); }

std::string SettingsView::row_text(std::size_t index) const {
    const auto specs = setting_specs();
    if (settings_ == nullptr || index >= specs.size()) return {};
    const SettingSpec& spec = specs[index];
    const std::int64_t value = settings_->value(spec.key);
    std::string out(spec.label);
    const int pad = label_width() - text_columns(spec.label) + kValueGap;
    out.append(static_cast<std::size_t>(pad), ' ');
    if (spec.type == SettingType::boolean) {
        out += value != 0 ? "[x]" : "[ ]";
    } else if (spec.type == SettingType::integer || spec.type == SettingType::choice) {
        out += setting_value_text(spec, value);
    } else {
        // A structured row: how many entries differ from the defaults.
        const Json* keymap = settings_->raw(spec.key);
        const std::size_t changed = keymap != nullptr && keymap->is_object() ? keymap->size() : 0;
        out += changed == 0 ? std::string("defaults") : std::format("{} changed", changed);
    }
    return out;
}

SettingsKeyResult SettingsView::handle_key(const KeyEvent& key) {
    SettingsKeyResult r;
    if (settings_ == nullptr) return r;
    const auto specs = setting_specs();
    const SettingSpec& spec = specs[selected_];
    const std::int64_t value = settings_->value(spec.key);
    const bool boolean = spec.type == SettingType::boolean;
    const bool integer = spec.type == SettingType::integer;
    const bool choice = spec.type == SettingType::choice;
    auto change = [&](std::int64_t v) {
        if (v < spec.min || v > spec.max || v == value) return;
        r.change = &spec;
        r.value = v;
    };
    // A choice steps through its names, wrapping at either end.
    auto step = [&](std::int64_t delta) {
        const std::int64_t n = spec.max - spec.min + 1;
        change(((value - spec.min + delta) % n + n) % n + spec.min);
    };
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
            if (const auto to = list_step(key.key, selected_, specs.size())) selected_ = to->index;
            break;
        case Key::Enter:
            if (boolean) {
                change(value != 0 ? 0 : 1);
            } else if (choice) {
                step(1);
            } else {
                r.edit = &spec;  // a prompt for a number, the editor of a structured row
            }
            break;
        case Key::Left:
            if (integer) change(value - 1);
            if (choice) step(-1);
            break;
        case Key::Right:
            if (integer) change(value + 1);
            if (choice) step(1);
            break;
        case Key::Char:
            if (key.ch == U' ' && key.mods == 0 && boolean) change(value != 0 ? 0 : 1);
            if (key.ch == U' ' && key.mods == 0 && choice) step(1);
            break;
        default: break;  // consumed
    }
    return r;
}

void SettingsView::render(Screen& screen, Rect area) {
    if (settings_ == nullptr || area.rows <= 0 || area.cols <= 0) return;
    const auto specs = setting_specs();
    const Attr plain = attr_for(Style::Default);
    const Attr selected = attr_for(Style::menu_selected);
    const int right = area.col + area.cols;
    for (int r = 0; r < area.rows; ++r) screen.fill(area.row + r, area.col, right, plain);

    // The selected setting's help goes under the list when there is room for both.
    const std::vector<std::string> help = wrap(specs[selected_].help, std::max(10, area.cols - 4));
    const int count = static_cast<int>(specs.size());
    int list_rows = std::min(count, area.rows);
    int help_rows = 0;
    if (area.rows >= 4) {
        help_rows = std::min(static_cast<int>(help.size()), area.rows - 2);
        list_rows = std::min(count, area.rows - 1 - help_rows);
    }
    const auto visible = static_cast<std::size_t>(std::max(1, list_rows));
    scroll_ = scroll_to_show(selected_, scroll_, visible);

    for (int r = 0; r < list_rows; ++r) {
        const std::size_t i = scroll_ + static_cast<std::size_t>(r);
        if (i >= specs.size()) break;
        const int row = area.row + r;
        const Attr a = i == selected_ ? selected : plain;
        screen.fill(row, area.col, right, a);
        if (i == selected_) screen.print(row, area.col, right, ">", a);
        screen.print(row, area.col + 2, right, row_text(i), a);
    }
    for (int r = 0; r < help_rows; ++r) {
        screen.print(area.row + list_rows + 1 + r, area.col + 2, right, help[static_cast<std::size_t>(r)], attr_for(Style::gutter));
    }
}

}  // namespace mod
