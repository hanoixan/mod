#include "ui/colors_view.hpp"

#include <algorithm>

#include "ui/list_cursor.hpp"

namespace mod {
namespace {

constexpr std::string_view kSample = "Sample";
constexpr std::size_t kGap = 3;

}  // namespace

void ColorsView::open(const ColorTheme& theme) {
    theme_ = &theme;
    rows_.clear();
    name_width_ = 0;
    std::string_view group;
    for (const ColorEntry& e : color_names()) {
        if (e.group != group) {
            group = e.group;
            rows_.push_back(Row{group, nullptr});
        }
        rows_.push_back(Row{{}, &e});
        name_width_ = std::max(name_width_, e.name.size());
    }
    selected_ = entry_near(0, 1);
    scroll_ = 0;
}

void ColorsView::close() { theme_ = nullptr; }

std::size_t ColorsView::entry_near(std::size_t from, int step) const {
    for (int dir : {step, -step}) {
        for (auto i = static_cast<std::ptrdiff_t>(std::min(from, rows_.size() - 1));
             i >= 0 && i < static_cast<std::ptrdiff_t>(rows_.size()); i += dir) {
            if (rows_[static_cast<std::size_t>(i)].entry != nullptr) return static_cast<std::size_t>(i);
        }
    }
    return 0;
}

std::string ColorsView::selected_name() const {
    if (selected_ >= rows_.size() || rows_[selected_].entry == nullptr) return {};
    return std::string(rows_[selected_].entry->name);
}

std::string ColorsView::row_text(std::size_t index) const {
    if (theme_ == nullptr || index >= rows_.size()) return {};
    const Row& row = rows_[index];
    if (row.entry == nullptr) return std::string(row.heading);
    std::string out(row.entry->name);
    out.append(name_width_ + kGap - out.size(), ' ');
    out += kSample;
    out.append(kGap, ' ');
    out += theme_->spec_of(row.entry->name);
    if (!theme_->is_default(row.entry->name)) out += " *";
    return out;
}

ColorsKeyResult ColorsView::handle_key(const KeyEvent& key) {
    ColorsKeyResult r;
    if (theme_ == nullptr) return r;
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
            // Headers cannot be selected: the nearest entry in the direction of the move.
            if (const auto to = list_step(key.key, selected_, rows_.size())) selected_ = entry_near(to->index, to->direction);
            break;
        case Key::Enter: r.edit = selected_name(); break;
        case Key::Delete:
            if (key.mods == 0) r.reset = selected_name();
            break;
        case Key::CtrlLetter:
            if (key.ch == U'r') r.reset = selected_name();
            break;
        case Key::Char:
            if ((key.mods & kAlt) && !(key.mods & kCtrl) && (key.ch == U'r' || key.ch == U'R')) r.reset_all = true;
            break;
        default: break;
    }
    return r;
}

void ColorsView::render(Screen& screen, Rect area) {
    if (theme_ == nullptr || area.rows <= 0 || area.cols <= 0) return;
    const Attr plain = attr_for(Style::Default);
    const Attr selected = attr_for(Style::list_selected);
    const int right = area.col + area.cols;
    const auto visible = static_cast<std::size_t>(area.rows);
    scroll_ = scroll_to_show(selected_, scroll_, visible);
    // Show a group's heading with its first entry when there is room.
    if (visible > 1 && scroll_ > 0 && scroll_ == selected_ && rows_[scroll_ - 1].entry == nullptr) --scroll_;
    for (int r = 0; r < area.rows; ++r) {
        const int row = area.row + r;
        screen.fill(row, area.col, right, plain);
        const std::size_t i = scroll_ + static_cast<std::size_t>(r);
        if (i >= rows_.size()) continue;
        const Row& entry = rows_[i];
        if (entry.entry == nullptr) {
            screen.print(row, area.col + 1, right, entry.heading, attr_for(Style::gutter_current));
            continue;
        }
        const Attr a = i == selected_ ? selected : plain;
        screen.fill(row, area.col, right, a);
        if (i == selected_) screen.print(row, area.col, right, ">", a);
        screen.print(row, area.col + 2, right, row_text(i), a);
        const Attr look = entry.entry->style ? theme_->attr(*entry.entry->style)
                                             : theme_->attr(Style::lsp_variable, entry.entry->modifier_bit);
        screen.print(row, area.col + 2 + static_cast<int>(name_width_ + kGap), right, kSample, look);
    }
}

}  // namespace mod
