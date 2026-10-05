#include "ui/doc_search_view.hpp"

#include <algorithm>
#include <format>
#include <span>

#include "text/utf8.hpp"
#include "ui/list_cursor.hpp"
#include "ui/theme.hpp"

namespace mod {
namespace {


}  // namespace

void DocSearchView::open(SearchFn search) {
    search_ = std::move(search);
    open_ = true;  // the query, results and selection are those of the last time
}

void DocSearchView::search() {
    results_ = search_ ? search_(query_) : DocSearchResult{};
    selected_ = 0;
    scroll_ = 0;
}

std::string DocSearchView::row_text(std::size_t index) const {
    if (index >= results_.matches.size()) return {};
    const DocMatch& m = results_.matches[index];
    return std::format("{}:{}  {}", m.page.generic_string(), m.line, m.excerpt);
}

void DocSearchView::handle_paste(std::string_view bytes) {
    if (const auto nl = bytes.find_first_of("\r\n"); nl != std::string_view::npos) bytes = bytes.substr(0, nl);
    query_ += bytes;
    search();
}

DocSearchKeyResult DocSearchView::handle_key(const KeyEvent& key) {
    DocSearchKeyResult r;
    if (!open_) return r;
    const std::size_t count = results_.matches.size();
    switch (key.key) {
        case Key::Escape:
            close();
            r.closed = true;
            break;
        case Key::Enter:
            if (count == 0) break;
            r.open = results_.matches[selected_];
            close();
            r.closed = true;
            break;
        case Key::Up:
        case Key::Down:
        case Key::Home:
        case Key::End:
        case Key::PageUp:
        case Key::PageDown:
            if (const auto to = list_step(key.key, selected_, count)) selected_ = to->index;
            break;
        case Key::Backspace:
            if (key.mods & kCtrl) {
                query_.clear();
            } else if (!query_.empty()) {
                const Decoded d = decode_before(std::as_bytes(std::span(query_.data(), query_.size())));
                query_.resize(query_.size() - std::max<std::size_t>(1, d.len));
            }
            search();
            break;
        case Key::Char:
            if ((key.mods & (kAlt | kCtrl)) == 0) {
                query_ += to_utf8(key.ch);
                search();
            }
            break;
        default: break;  // consumed
    }
    return r;
}

void DocSearchView::render(Screen& screen, Rect area) {
    if (!open_ || area.rows <= 0 || area.cols <= 0) return;
    const Attr plain = attr_for(Style::Default);
    const Attr selected = attr_for(Style::menu_selected);
    const int right = area.col + area.cols;
    for (int r = 0; r < area.rows; ++r) screen.fill(area.row + r, area.col, right, plain);

    int col = screen.print(area.row, area.col + 1, right, "Search the manual: ", attr_for(Style::gutter_current));
    col = screen.print(area.row, col, right, query_, plain);
    screen.set_cursor(area.row, std::min(col, right - 1), true);
    if (!query_.empty()) {
        const std::size_t n = results_.matches.size();
        const std::string count = results_.capped ? std::format("{}+ matches", n) : std::format("{} match{}", n, n == 1 ? "" : "es");
        const int at = right - 1 - static_cast<int>(count.size());
        if (at > col + 1) screen.print(area.row, at, right, count, attr_for(Style::gutter));
    }

    const int list_rows = area.rows - 1;
    if (list_rows <= 0) return;
    if (results_.matches.empty()) {
        screen.print(area.row + 1, area.col + 2, right, query_.empty() ? "type to search every page" : "no matches", attr_for(Style::gutter));
        return;
    }
    const auto visible = static_cast<std::size_t>(list_rows);
    scroll_ = scroll_to_show(selected_, scroll_, visible);
    for (int r = 0; r < list_rows; ++r) {
        const std::size_t i = scroll_ + static_cast<std::size_t>(r);
        if (i >= results_.matches.size()) break;
        const int row = area.row + 1 + r;
        const Attr a = i == selected_ ? selected : plain;
        screen.fill(row, area.col, right, a);
        if (i == selected_) screen.print(row, area.col, right, ">", a);
        screen.print(row, area.col + 2, right, row_text(i), a);
    }
}

}  // namespace mod
