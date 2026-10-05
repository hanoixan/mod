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

namespace {

// The newest child continues a change's line; none for a leaf.
std::optional<NodeId> newest_child(const NodeInfo& info) {
    if (info.children.empty()) return std::nullopt;
    return *std::max_element(info.children.begin(), info.children.end());
}

}  // namespace

void HistoryView::open(const UndoTree& tree) {
    tree_ = &tree;
    rows_.clear();
    open_.clear();
    open_trees_.clear();
    redo_path_.clear();
    selected_ = 0;
    scroll_ = 0;

    // The trees top to bottom: the others, oldest (by newest id) first, then the current one.
    const std::optional<NodeId> current = tree.current();
    current_root_ = current ? std::optional<NodeId>(tree.root_of(*current)) : std::nullopt;
    struct Entry {
        NodeId root;
        NodeId newest;
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
        if (r != current_root_) others.push_back({r, newest_of(r)});
    }
    for (const NodeId r : tree.retired_roots()) others.push_back({r, newest_of(r)});
    std::sort(others.begin(), others.end(), [](const Entry& a, const Entry& b) { return a.newest < b.newest; });
    roots_.clear();
    for (const Entry& e : others) roots_.push_back(e.root);
    if (current_root_) roots_.push_back(*current_root_);

    if (current && !tree.is_retired(*current_root_)) {
        for (std::optional<NodeId> n = tree.node_info(*current).preferred_child; n; n = tree.node_info(*n).preferred_child) {
            redo_path_.insert(*n);
        }
    }
    // Open only the branches the current state lies in: every change it branched off from.
    for (NodeId n = current.value_or(0); current;) {
        const NodeId parent = tree.meta(n).parent;
        if (parent == kNoParent || !tree.contains(parent)) break;
        if (newest_child(tree.node_info(parent)) != n) open_.insert(parent);
        n = parent;
    }
    rebuild();
    if (current) {
        select_node(*current);
    } else if (!rows_.empty()) {
        selected_ = rows_.size() - 1;
    }
    keep_visible();
}

void HistoryView::rebuild() {
    const bool had = selected_ < rows_.size();
    const NodeId keep = had ? rows_[selected_].node : 0;
    rows_.clear();
    for (const NodeId root : roots_) emit_tree(root, root != current_root_ || tree_->is_retired(root));
    if (rows_.empty()) {
        selected_ = 0;
        return;
    }
    selected_ = std::min(selected_, rows_.size() - 1);
    if (had) select_node(keep);
}

void HistoryView::emit_tree(NodeId root, bool read_only) {
    const UndoTree& tree = *tree_;
    const std::optional<NodeId> current = tree.current();
    auto make_row = [&](NodeId n, const NodeInfo& info) {
        HistoryRow row;
        row.node = n;
        row.is_current = !read_only && current == n;
        row.is_save_point = info.is_save_point;
        row.on_redo_path = !read_only && redo_path_.contains(n);
        row.read_only = read_only;
        return row;
    };
    if (root != current_root_ && !open_trees_.contains(root)) {
        // Closed: one row, the tree's latest change on its newest line.
        NodeId tip = root;
        for (std::optional<NodeId> c = newest_child(tree.node_info(tip)); c; c = newest_child(tree.node_info(*c))) tip = *c;
        HistoryRow row = make_row(tip, tree.node_info(tip));
        row.expandable = true;
        row.graph = "○> ";
        rows_.push_back(std::move(row));
        return;
    }
    // A line runs down the newest children; an open change's side branches (its older
    // children, oldest first) come right under it, one level deeper, before its line goes on.
    struct Line {
        NodeId start;
        int depth;
        bool first;  // `start` begins a branch: drawn with ├─
        std::optional<NodeId> from;
    };
    std::vector<Line> stack{{root, 0, false, std::nullopt}};
    while (!stack.empty()) {
        const Line line = stack.back();
        stack.pop_back();
        bool first = line.first;
        for (NodeId n = line.start;;) {
            const NodeInfo info = tree.node_info(n);
            const std::optional<NodeId> next = newest_child(info);
            std::vector<NodeId> sides;
            for (const NodeId c : info.children) {
                if (c != next) sides.push_back(c);
            }
            std::sort(sides.begin(), sides.end());
            HistoryRow row = make_row(n, info);
            row.depth = line.depth;
            row.branch_from = line.from;
            row.expandable = !sides.empty();
            row.expanded = row.expandable && open_.contains(n);
            for (int k = 1; k < line.depth; ++k) row.graph += "│ ";
            if (line.depth > 0) row.graph += first ? "├─" : "│ ";
            row.graph += row.is_current ? "●" : "○";
            if (row.expandable && !row.expanded) row.graph += '>';
            row.graph += ' ';
            const bool expanded = row.expanded;
            rows_.push_back(std::move(row));
            first = false;
            if (expanded) {
                if (next) stack.push_back({*next, line.depth, false, line.from});
                for (auto it = sides.rbegin(); it != sides.rend(); ++it) stack.push_back({*it, line.depth + 1, true, n});
                break;
            }
            if (!next) break;
            n = *next;
        }
    }
}

std::string HistoryView::row_text(std::size_t index) const {
    const HistoryRow& r = rows_.at(index);
    std::string out = r.graph;
    if (tree_ == nullptr) return out;
    const NodeMeta& meta = tree_->meta(r.node);
    out += std::format("{} {}", r.node, kind_label(meta.kind));
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

void HistoryView::select_node(NodeId node) {
    for (std::size_t i = 0; i < rows_.size(); ++i) {
        if (rows_[i].node == node) {
            selected_ = i;
            return;
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
    const std::size_t last = rows_.empty() ? 0 : rows_.size() - 1;
    switch (key.key) {
        case Key::Up:
            if (selected_ > 0) --selected_;
            break;
        case Key::Down: selected_ = std::min(selected_ + 1, last); break;
        case Key::PageUp: selected_ = selected_ > page ? selected_ - page : 0; break;
        case Key::PageDown: selected_ = std::min(selected_ + page, last); break;
        case Key::Home: selected_ = 0; break;
        case Key::End: selected_ = last; break;
        case Key::Right: {
            // Like a folder: opens its branches, or, already open, steps into the first one.
            const HistoryRow& r = rows_[selected_];
            if (r.expandable && !r.expanded) {
                const NodeId root = tree_->root_of(r.node);
                if (root != current_root_ && !open_trees_.contains(root)) {
                    open_trees_.insert(root);
                } else {
                    open_.insert(r.node);
                }
                rebuild();
            } else if (r.expanded && selected_ < last) {
                ++selected_;
            }
            break;
        }
        case Key::Left: {
            // Closes its branches; else goes to the change its branch split from; else, in an
            // older tree, closes the tree onto its row.
            const HistoryRow& r = rows_[selected_];
            const NodeId node = r.node;
            if (r.expandable && r.expanded) {
                open_.erase(node);
                rebuild();
            } else if (r.branch_from) {
                select_node(*r.branch_from);
            } else if (const NodeId root = tree_->root_of(node); open_trees_.erase(root) > 0) {
                rebuild();
                for (std::size_t i = 0; i < rows_.size(); ++i) {
                    if (tree_->root_of(rows_[i].node) == root) selected_ = i;
                }
            }
            break;
        }
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

void HistoryView::render(Screen& screen, Rect area, bool focused) {
    if (!is_open()) return;
    // The footer takes the bottom rows when the pane is tall enough to keep a list too.
    const bool footer = area.rows >= kMinRowsForFooter;
    const int list_rows = footer ? area.rows - kFooterRows : area.rows;
    view_rows_ = std::max(1, list_rows);
    keep_visible();
    // The list looks like the folder tree: plain rows and the list highlight; the footer's
    // commands keep the menu's look.
    const Attr base = attr_for(Style::menu);
    const Attr plain = attr_for(Style::Default);
    const Attr selected = attr_for(focused ? Style::list_selected : Style::list_selected_unfocused);
    Attr read_only = plain;
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
            screen.fill(row, area.col, right, plain);
            continue;
        }
        const HistoryRow& hr = rows_[i];
        const Attr a = i == selected_ ? selected : (hr.read_only ? read_only : plain);
        screen.fill(row, area.col, right, a);
        screen.print(row, area.col, right, row_text(i), a);
        if (hr.is_current) {
            const int star = 2 * hr.depth;  // the ●, after two columns a level
            Attr marker = attr_for(Style::gutter_current);
            marker.fg = a.fg;
            marker.bg = a.bg;
            marker.flags |= a.flags;
            if (area.col + star < right) screen.put(row, area.col + star, "●", 1, marker);
        }
    }
}

}  // namespace mod
