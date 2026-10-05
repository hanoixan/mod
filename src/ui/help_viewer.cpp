#include "ui/help_viewer.hpp"

#include <algorithm>
#include <fstream>
#include <iterator>
#include <set>

#include "ui/theme.hpp"

namespace mod {
namespace {

constexpr std::uintmax_t kMaxPageBytes = std::uintmax_t{4} << 20;

}  // namespace

Status HelpViewer::open(const std::filesystem::path& root) {
    if (page_.empty() || root != root_) {
        root_ = root;
        back_.clear();
        if (auto s = load("index.md"); !s) return s;
        top_ = 0;
        selected_.reset();
    }
    open_ = true;
    return {};
}

Status HelpViewer::load(const std::filesystem::path& page) {
    const std::filesystem::path file = root_ / page;
    std::error_code ec;
    const auto size = std::filesystem::file_size(file, ec);
    if (ec) return std::unexpected(make_error(ErrorCode::not_found, "no such page"));
    if (size > kMaxPageBytes) return std::unexpected(make_error(ErrorCode::too_large, "larger than 4 MiB"));
    std::ifstream in(file, std::ios::binary);
    if (!in) return std::unexpected(make_error(ErrorCode::io, "cannot read it"));
    text_.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    page_ = page.lexically_normal();
    rendered_ = render_markdown(text_, width_);
    return {};
}

void HelpViewer::relayout() {
    const std::uint32_t source_top = top_ < rendered_.lines.size() ? rendered_.lines[top_].source_line : 1;
    rendered_ = render_markdown(text_, width_);
    top_ = line_for_source(source_top);
}

// The first rendered line of the last source line at or before `source_line`.
std::size_t HelpViewer::line_for_source(std::uint32_t source_line) const {
    std::uint32_t best = 0;
    for (const RenderedLine& l : rendered_.lines)
        if (l.source_line <= source_line) best = std::max(best, l.source_line);
    for (std::size_t i = 0; i < rendered_.lines.size(); ++i)
        if (rendered_.lines[i].source_line == best) return i;
    return 0;
}

std::size_t HelpViewer::max_top() const {
    const auto rows = static_cast<std::size_t>(std::max(1, rows_));
    return rendered_.lines.size() > rows ? rendered_.lines.size() - rows : 0;
}

bool HelpViewer::link_visible(std::size_t link) const {
    const auto& pieces = rendered_.links[link].pieces;
    if (pieces.empty()) return false;
    const std::size_t line = pieces.front().line;
    return line >= top_ && line < top_ + static_cast<std::size_t>(std::max(1, rows_));
}

std::optional<std::size_t> HelpViewer::first_visible_link() const {
    for (std::size_t i = 0; i < rendered_.links.size(); ++i)
        if (link_visible(i)) return i;
    return std::nullopt;
}

// A highlighted link that went out of sight gives way to the first one in sight.
void HelpViewer::keep_selection_visible() {
    if (!selected_ || !link_visible(*selected_)) selected_ = first_visible_link();
}

void HelpViewer::scroll_to(std::size_t top) {
    top_ = std::min(top, max_top());
    keep_selection_visible();
}

std::optional<std::string> HelpViewer::selected_target() const {
    if (!selected_ || *selected_ >= rendered_.links.size()) return std::nullopt;
    return rendered_.links[*selected_].target;
}

HelpViewer::Place HelpViewer::here() const {
    return Place{page_, top_ < rendered_.lines.size() ? rendered_.lines[top_].source_line : 1, selected_};
}

Status HelpViewer::show(const std::filesystem::path& page, std::uint64_t source_line) {
    const Place from = here();
    if (auto s = load(page); !s) return s;
    back_.push_back(from);
    top_ = line_for_source(static_cast<std::uint32_t>(std::min<std::uint64_t>(source_line, UINT32_MAX)));
    selected_.reset();
    return {};
}

std::string HelpViewer::follow(const std::string& target) {
    if (target.find("://") != std::string::npos || target.starts_with("mailto:")) return "web address, not followed: " + target;
    const std::size_t hash = target.find('#');
    const std::string path = target.substr(0, hash);
    const std::string anchor = hash == std::string::npos ? std::string() : target.substr(hash + 1);
    const Place from = here();
    if (!path.empty()) {
        const std::filesystem::path page = (page_.parent_path() / path).lexically_normal();
        if (page.extension() != ".md") return "not a page of the manual: " + page.generic_string();
        if (auto s = load(page); !s) return "cannot open " + page.generic_string() + ": " + s.error().message;
    }
    back_.push_back(from);
    top_ = 0;
    for (const auto& [slug, line] : rendered_.anchors) {
        if (!anchor.empty() && slug == anchor) {
            top_ = std::min(line, rendered_.lines.empty() ? 0 : rendered_.lines.size() - 1);
            break;
        }
    }
    selected_.reset();
    return {};
}

void HelpViewer::back() {
    if (back_.empty()) return;
    const Place p = back_.back();
    back_.pop_back();
    if (p.page != page_ && !load(p.page)) return;
    top_ = line_for_source(p.source_top);
    selected_ = p.link;
}

HelpKeyResult HelpViewer::handle_key(const KeyEvent& key) {
    HelpKeyResult r;
    used_ = true;
    const auto rows = static_cast<std::size_t>(std::max(1, rows_));
    const std::size_t n = rendered_.links.size();
    switch (key.key) {
        case Key::Escape:
            close();
            r.closed = true;
            break;
        case Key::Down: {
            std::optional<std::size_t> next;
            for (std::size_t i = selected_ ? *selected_ + 1 : 0; i < n && !next; ++i)
                if (link_visible(i)) next = i;
            if (next) {
                selected_ = next;
            } else if (top_ < max_top()) {
                scroll_to(top_ + 1);
            }
            break;
        }
        case Key::Up: {
            std::optional<std::size_t> prev;
            for (std::size_t i = selected_ ? *selected_ : 0; i-- > 0 && !prev;)
                if (link_visible(i)) prev = i;
            if (prev) {
                selected_ = prev;
            } else if (top_ > 0) {
                scroll_to(top_ - 1);
            }
            break;
        }
        case Key::Right:
        case Key::Enter:
            if (const auto target = selected_target()) r.message = follow(*target);
            break;
        case Key::Left:
        case Key::Backspace: back(); break;
        case Key::PageDown: scroll_to(top_ + rows); break;
        case Key::PageUp: scroll_to(top_ > rows ? top_ - rows : 0); break;
        case Key::Home: scroll_to(0); break;
        case Key::End: scroll_to(max_top()); break;
        case Key::CtrlLetter:
            if (key.ch == U'f') {
                r.search = true;
            } else {
                used_ = false;
            }
            break;
        case Key::Char:
            if (key.mods & (kAlt | kCtrl)) {
                used_ = false;
            } else if (key.ch == U'/') {
                r.search = true;
            } else if (key.ch == U' ') {
                scroll_to(top_ + rows);
            }  // other text means nothing here, and is not an edit
            break;
        default: used_ = false; break;
    }
    return r;
}

void HelpViewer::render(Screen& screen, Rect area) {
    if (area.rows <= 0 || area.cols <= 0) return;
    rows_ = area.rows;
    if (area.cols != width_) {
        width_ = area.cols;
        relayout();
    }
    const Attr plain = on_page(attr_for(Style::Default));
    const Attr link_look = on_page(attr_for(Style::md_link_text));
    const Attr selected_look = attr_for(Style::list_selected);
    const int right = area.col + area.cols;
    for (int r = 0; r < area.rows; ++r) {
        const int row = area.row + r;
        screen.fill(row, area.col, right, plain);
        const std::size_t i = top_ + static_cast<std::size_t>(r);
        if (i >= rendered_.lines.size()) continue;
        const RenderedLine& line = rendered_.lines[i];
        // Cut the line where a span or a link piece starts or ends, and draw each part.
        std::set<std::size_t> cuts{0, line.text.size()};
        for (const RenderSpan& s : line.spans) cuts.insert({s.begin, s.end});
        struct LinkSpan {
            std::size_t begin, end;
            bool selected;
        };
        std::vector<LinkSpan> pieces;
        for (std::size_t l = 0; l < rendered_.links.size(); ++l) {
            for (const LinkPiece& p : rendered_.links[l].pieces) {
                if (p.line != i) continue;
                pieces.push_back({p.begin, p.end, selected_ == l});
                cuts.insert({p.begin, p.end});
            }
        }
        int col = area.col;
        for (auto it = cuts.begin(); std::next(it) != cuts.end(); ++it) {
            const std::size_t b = *it;
            const std::size_t e = *std::next(it);
            Attr a = plain;
            for (const RenderSpan& s : line.spans)
                if (b >= s.begin && b < s.end) a = on_page(attr_for(s.style));
            for (const LinkSpan& p : pieces)
                if (b >= p.begin && b < p.end) a = p.selected ? selected_look : link_look;
            col = screen.print(row, col, right, std::string_view(line.text).substr(b, e - b), a);
        }
    }
}

}  // namespace mod
