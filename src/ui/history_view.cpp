#include "ui/history_view.hpp"

#include <algorithm>
#include <chrono>
#include <format>
#include <unordered_set>

#include "ui/theme.hpp"

namespace mod {
namespace {

// The commands under the list: a rule, then one row each. The first letter is the key.
// The footer's rows and the position of each one's key letter; the checkbox is filled in.
struct FooterRow {
    std::string_view label;
    std::size_t accel;
};
constexpr FooterRow kFooter[] = {{"Clear History…", 0}, {"Trim History…", 0}, {"[ ] Persist History", 4}};
constexpr int kFooterRows = 1 + static_cast<int>(std::size(kFooter));
constexpr int kMinRowsForFooter = 6;

constexpr std::string_view kReadOnlyMessage = "read-only: history from before a reload or Clear History";

std::string_view kind_label(EditKind k) {
    switch (k) {
        case EditKind::typing: return "typed";
        case EditKind::delete_: return "delete";
        case EditKind::paste: return "paste";
        case EditKind::cut: return "cut";
        case EditKind::replace: return "replace";
        case EditKind::replace_all: return "replace_all";
        case EditKind::newline: return "newline";
        case EditKind::indent: return "indent";
        case EditKind::other: return "other";
    }
    return "other";
}

std::string relative_time(std::int64_t now_ms, std::int64_t then_ms) {
    const std::int64_t s = std::max<std::int64_t>(0, now_ms - then_ms) / 1000;
    if (s < 60) return "just now";
    if (s < 3600) return std::format("{} min ago", s / 60);
    if (s < 86400) return std::format("{} h ago", s / 3600);
    return std::format("{} days ago", s / 86400);
}

}  // namespace

PaneWidth history_pane_width(int columns) {
    if (columns < 60) return {std::max(1, columns - 2), true};
    return {std::clamp(columns / 3, 20, 40), false};
}

HistoryView::HistoryView(Clock now_ms) : now_ms_(std::move(now_ms)) {}

std::int64_t HistoryView::now() const {
    if (now_ms_) return now_ms_();
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

void HistoryView::open(const UndoTree& tree) {
    tree_ = &tree;
    rows_.clear();
    selected_ = 0;
    scroll_ = 0;

    // Trees in display order: the current tree, then the others by their newest id.
    const std::optional<NodeId> current = tree.current();
    const bool has_current = current.has_value();
    const NodeId current_root = has_current ? tree.root_of(*current) : NodeId{};
    struct Entry {
        NodeId root;
        NodeId newest;
        bool read_only;
    };
    std::vector<Entry> others;
    auto newest_of = [&](NodeId root) {
        NodeId best = root;
        std::vector<NodeId> stack{root};
        while (!stack.empty()) {
            const NodeId n = stack.back();
            stack.pop_back();
            best = std::max(best, n);
            const NodeInfo info = tree.node_info(n);
            stack.insert(stack.end(), info.children.begin(), info.children.end());
        }
        return best;
    };
    for (const NodeId r : tree.roots()) {
        if (has_current && r == current_root) continue;
        others.push_back({r, newest_of(r), true});
    }
    for (const NodeId r : tree.retired_roots()) others.push_back({r, newest_of(r), true});
    std::sort(others.begin(), others.end(), [](const Entry& a, const Entry& b) { return a.newest > b.newest; });

    if (has_current) build_tree(current_root, tree.is_retired(current_root));
    for (const Entry& e : others) build_tree(e.root, e.read_only);

    // Select the current node's row.
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].is_current) {
            selected_ = i;
            break;
        }
    }
    if (!rows_.empty() && !rows_[selected_].node) select(selected_, 1);
    keep_visible();
}

void HistoryView::build_tree(NodeId root, bool read_only) {
    const UndoTree& tree = *tree_;
    std::vector<NodeId> ids;
    std::vector<NodeId> stack{root};
    while (!stack.empty()) {
        const NodeId n = stack.back();
        stack.pop_back();
        ids.push_back(n);
        const NodeInfo info = tree.node_info(n);
        stack.insert(stack.end(), info.children.begin(), info.children.end());
    }
    std::sort(ids.begin(), ids.end(), std::greater<>());

    const std::optional<NodeId> current = tree.current();
    std::unordered_set<NodeId> redo_path;
    if (current && !read_only && tree.root_of(*current) == root) {
        std::optional<NodeId> n = tree.node_info(*current).preferred_child;
        while (n) {
            redo_path.insert(*n);
            n = tree.node_info(*n).preferred_child;
        }
    }

    std::vector<std::optional<NodeId>> lanes;  // the parent each open lane waits for
    auto trim = [&] {
        while (!lanes.empty() && !lanes.back()) lanes.pop_back();
    };
    for (const NodeId n : ids) {
        std::vector<std::size_t> waiting;
        for (std::size_t k = 0; k < lanes.size(); ++k) {
            if (lanes[k] == n) waiting.push_back(k);
        }
        std::size_t lane = 0;
        if (waiting.empty()) {
            while (lane < lanes.size() && lanes[lane]) ++lane;  // the leftmost free lane
            if (lane == lanes.size()) lanes.emplace_back();
        } else {
            lane = waiting[0];
            if (waiting.size() > 1) {
                // One connector row closes the other lanes into this one.
                std::string g(2 * lanes.size(), ' ');
                for (std::size_t k = 0; k < lanes.size(); ++k) {
                    if (lanes[k]) g[2 * k] = '|';
                }
                for (std::size_t w = 1; w < waiting.size(); ++w) {
                    g[2 * waiting[w]] = ' ';
                    g[2 * waiting[w] - 1] = '/';
                    lanes[waiting[w]].reset();
                }
                while (!g.empty() && g.back() == ' ') g.pop_back();
                HistoryRow c;
                c.graph = std::move(g);
                c.read_only = read_only;
                rows_.push_back(std::move(c));
                trim();
            }
        }
        const NodeInfo info = tree.node_info(n);
        HistoryRow row;
        row.node = n;
        row.is_current = !read_only && current && *current == n;
        row.is_save_point = info.is_save_point;
        row.on_redo_path = redo_path.contains(n);
        row.read_only = read_only;
        std::size_t width = lane + 1;
        for (std::size_t k = 0; k < lanes.size(); ++k) {
            if (lanes[k]) width = std::max(width, k + 1);
        }
        row.graph.assign(2 * width, ' ');
        for (std::size_t k = 0; k < width && k < lanes.size(); ++k) {
            if (lanes[k]) row.graph[2 * k] = '|';
        }
        row.graph[2 * lane] = row.is_current ? '*' : 'o';
        rows_.push_back(std::move(row));
        const NodeId parent = info.meta.parent;
        if (parent != kNoParent && tree.contains(parent)) {
            lanes[lane] = parent;
        } else {
            lanes[lane].reset();
        }
        trim();
    }
}

std::string HistoryView::row_text(std::size_t index) const {
    const HistoryRow& r = rows_.at(index);
    std::string out = r.graph;
    if (!r.node || tree_ == nullptr) return out;
    const NodeMeta& meta = tree_->meta(*r.node);
    out += std::format("{} {}", *r.node, kind_label(meta.kind));
    if (r.is_save_point) out += " saved";
    out += "  ";
    out += relative_time(now(), meta.time_unix_ms);
    return out;
}

void HistoryView::close() {
    tree_ = nullptr;
    rows_.clear();
    rows_.shrink_to_fit();
    selected_ = 0;
    scroll_ = 0;
}

// Moves to `index`, or the nearest node row from it in `direction` (then the other way).
void HistoryView::select(std::size_t index, int direction) {
    if (rows_.empty()) return;
    index = std::min(index, rows_.size() - 1);
    for (int pass = 0; pass < 2; ++pass) {
        const int dir = pass == 0 ? direction : -direction;
        std::size_t i = index;
        for (;;) {
            if (rows_[i].node) {
                selected_ = i;
                return;
            }
            if (dir > 0 ? i + 1 >= rows_.size() : i == 0) break;
            i = dir > 0 ? i + 1 : i - 1;
        }
    }
}

void HistoryView::keep_visible() {
    const auto h = static_cast<std::size_t>(std::max(1, view_rows_));
    if (selected_ < scroll_) scroll_ = selected_;
    if (selected_ >= scroll_ + h) scroll_ = selected_ + 1 - h;
}

HistoryKeyResult HistoryView::handle_key(const KeyEvent& key) {
    HistoryKeyResult result;
    if (!is_open()) return result;
    // An empty history has no row to move to or jump to; Escape and the letters still apply.
    if (rows_.empty() && key.key != Key::Escape && key.key != Key::Char) return result;
    const auto page = static_cast<std::size_t>(std::max(1, view_rows_ - 1));
    switch (key.key) {
        case Key::Up:
            if (selected_ > 0) select(selected_ - 1, -1);
            if (!rows_[selected_].node) select(selected_, 1);
            break;
        case Key::Down: select(selected_ + 1, 1); break;
        case Key::PageUp: select(selected_ > page ? selected_ - page : 0, -1); break;
        case Key::PageDown: select(selected_ + page, 1); break;
        case Key::Home: select(0, 1); break;
        case Key::End: select(rows_.size() - 1, -1); break;
        case Key::Enter:
            if (rows_[selected_].read_only) {
                result.message = std::string(kReadOnlyMessage);
            } else {
                result.jump = rows_[selected_].node;
            }
            break;
        case Key::Escape:
            close();
            result.closed = true;
            return result;
        case Key::Char:
            if (key.mods & (kAlt | kCtrl)) break;
            if (key.ch == U'c' || key.ch == U'C') result.command = CommandId::ClearHistory;
            if (key.ch == U't' || key.ch == U'T') result.command = CommandId::TrimHistory;
            if (key.ch == U'p' || key.ch == U'P') result.command = CommandId::TogglePersistHistory;
            break;
        default: break;  // consumed: typing never reaches the document
    }
    keep_visible();
    return result;
}

void HistoryView::render(Screen& screen, Rect area) {
    if (!is_open()) return;
    // The footer takes the bottom rows when the pane is tall enough to keep a list too.
    const bool footer = area.rows >= kMinRowsForFooter;
    const int list_rows = footer ? area.rows - kFooterRows : area.rows;
    view_rows_ = std::max(1, list_rows);
    keep_visible();
    const Attr base = attr_for(Style::menu);
    const Attr selected = attr_for(Style::menu_selected);
    Attr read_only = base;
    read_only.flags |= attr_for(Style::history_read_only).flags;
    const int right = area.col + area.cols;
    if (footer) {
        const int rule = area.row + list_rows;
        screen.fill(rule, area.col, right, base);
        for (int c = area.col; c < right; ++c) screen.put(rule, c, "─", 1, base);
        Attr accel = base;
        accel.flags |= kUnderline;
        for (std::size_t k = 0; k < std::size(kFooter); ++k) {
            const int row = rule + 1 + static_cast<int>(k);
            screen.fill(row, area.col, right, base);
            std::string label(kFooter[k].label);
            if (label.starts_with("[ ]") && persist_) label[1] = 'x';
            const std::size_t a = kFooter[k].accel;
            int col = screen.print(row, area.col, right, std::string_view(label).substr(0, a), base);
            col = screen.print(row, col, right, std::string_view(label).substr(a, 1), accel);
            screen.print(row, col, right, std::string_view(label).substr(a + 1), base);
        }
    }
    for (int r = 0; r < list_rows; ++r) {
        const int row = area.row + r;
        const std::size_t i = scroll_ + static_cast<std::size_t>(r);
        if (i >= rows_.size()) {
            screen.fill(row, area.col, right, base);
            continue;
        }
        const HistoryRow& hr = rows_[i];
        const Attr a = i == selected_ ? selected : (hr.read_only ? read_only : base);
        screen.fill(row, area.col, right, a);
        screen.print(row, area.col, right, row_text(i), a);
        if (hr.is_current) {
            const auto star = static_cast<int>(hr.graph.find('*'));
            Attr marker = attr_for(Style::gutter_current);
            marker.fg = a.fg;
            marker.bg = a.bg;
            marker.flags |= a.flags;
            if (area.col + star < right) screen.put(row, area.col + star, "*", 1, marker);
        }
    }
}

}  // namespace mod
