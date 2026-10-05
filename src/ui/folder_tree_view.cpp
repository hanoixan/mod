#include "ui/folder_tree_view.hpp"

#include <algorithm>
#include <string>

#include "text/utf8.hpp"
#include "ui/editor_view.hpp"
#include "ui/theme.hpp"

namespace mod {

namespace {

// A row's text: two columns of indent a level, the folder's marker or room for one, the name.
std::string row_text(const TreeRow& r) {
    std::string out(static_cast<std::size_t>(r.depth) * 2, ' ');
    out += r.dir ? (r.expanded ? "▾ " : "▸ ") : "  ";
    return out + r.name;
}

}  // namespace

TreeKeyResult FolderTreeView::handle_key(const KeyEvent& key) {
    TreeKeyResult r;
    message_.clear();
    if (tree_ == nullptr) return r;
    const TreeRow* row = tree_->selected_row();
    const bool file = row != nullptr && !row->dir;
    if (key.key == Key::Right && key.mods == kShift) return {TreeKey::back, {}};
    if (key.mods != 0) return r;
    const auto page = static_cast<long>(std::max(1, list_rows_));
    switch (key.key) {
        case Key::Escape: return {TreeKey::leave, {}};
        case Key::Up: tree_->move(-1); break;
        case Key::Down: tree_->move(1); break;
        case Key::PageUp: tree_->move(-page); break;
        case Key::PageDown: tree_->move(page); break;
        case Key::Home: tree_->select(0); break;
        case Key::End: tree_->select(tree_->rows().size()); break;
        case Key::Right: tree_->expand(); break;
        case Key::Left: tree_->collapse(); break;
        case Key::Enter:
            if (file) return {TreeKey::open, row->path};
            tree_->toggle();
            break;
        case Key::Char:
            if (key.ch == U' ' && file) return {TreeKey::preview, row->path};
            return r;
        default: return r;
    }
    return {TreeKey::moved, {}};
}

void FolderTreeView::render(Screen& screen, Rect area, bool focused) {
    if (tree_ == nullptr || area.rows <= 0 || area.cols <= 0) return;
    const Attr plain = attr_for(Style::Default);
    const Attr selected = focused ? attr_for(Style::menu_selected) : attr_for(Style::selection);
    list_rows_ = std::max(1, area.rows - 1);
    const auto& rows = tree_->rows();
    const std::size_t sel = tree_->selected();
    const auto visible = static_cast<std::size_t>(list_rows_);
    if (sel < top_) top_ = sel;
    if (sel >= top_ + visible) top_ = sel + 1 - visible;
    // Sideways: the selected row's whole text in view, panned back as soon as it fits.
    if (const TreeRow* r = tree_->selected_row()) {
        const int width = text_columns(row_text(*r));
        hscroll_ = width <= area.cols ? 0 : width - area.cols;
    }
    for (int i = 0; i < list_rows_; ++i) {
        const int row = area.row + i;
        screen.fill(row, area.col, area.col + area.cols, plain);
        const std::size_t index = top_ + static_cast<std::size_t>(i);
        if (index >= rows.size()) continue;
        const TreeRow& r = rows[index];
        Attr a = r.hidden ? Attr{plain.fg, plain.bg, static_cast<std::uint8_t>(plain.flags | kDim)} : plain;
        if (index == sel) a = selected;
        // Drop the panned-off columns, a character at a time.
        const std::string text = row_text(r);
        std::size_t from = 0;
        for (int skipped = 0; skipped < hscroll_ && from < text.size();) {
            const Decoded d = decode(std::as_bytes(std::span(text.data() + from, text.size() - from)));
            skipped += d.valid ? std::max(0, display_width(d, 0, 1)) : 1;
            from += std::max<std::size_t>(1, d.len);
        }
        if (index == sel) screen.fill(row, area.col, area.col + area.cols, a);
        screen.print(row, area.col, area.col + area.cols, std::string_view(text).substr(from), a);
    }
    const std::string_view hints = text_columns(kHints) + 1 <= area.cols ? kHints : kShortHints;
    const std::string_view line = !message_.empty() ? std::string_view(message_) : !tree_->message().empty() ? std::string_view(tree_->message()) : hints;
    const int bottom = area.row + area.rows - 1;
    const Attr bar = attr_for(Style::status);
    screen.fill(bottom, area.col, area.col + area.cols, bar);
    screen.print(bottom, area.col + 1, area.col + area.cols, line, bar);
}

}  // namespace mod
