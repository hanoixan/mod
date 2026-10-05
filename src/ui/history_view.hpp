#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

#include "app/commands.hpp"
#include "edit/undo_tree.hpp"
#include "ui/input.hpp"
#include "ui/screen.hpp"

namespace mod {

struct PaneWidth {
    int width = 0;            // inside the frame
    bool full_width = false;  // the text area is not drawn
};

// The pane's row width for a terminal `columns` wide.
PaneWidth history_pane_width(int columns);

// One change's row. `graph` is what is drawn before its id: the connectors of the branches
// it sits in (`│ `, and `├─` on a branch's first change), then ○ (● for the current state),
// then '>' while it has closed branches, then a space.
struct HistoryRow {
    NodeId node = 0;
    std::string graph;
    int depth = 0;                       // how many branches deep: two columns each
    bool expandable = false;             // it has side branches (or is an older tree's closed row)
    bool expanded = false;               // ...and they are shown
    std::optional<NodeId> branch_from;   // the change this row's branch split from; none at depth 0
    bool is_current = false;
    bool is_save_point = false;
    bool on_redo_path = false;
    bool read_only = false;
};

struct HistoryKeyResult {
    std::optional<NodeId> jump;  // Enter on a node that can become current
    bool closed = false;         // Esc closed the panel
    std::string message;         // a refusal for the status line
    std::optional<CommandId> command;  // C, T or P: ClearHistory, TrimHistory or TogglePersistHistory, for App to run
};

// The undo-history panel: the whole forest as a tree list, oldest at the top. Each line
// follows the newest child; older children are side branches under the change they split
// from, opened and closed like folders. Main thread.
class HistoryView {
public:
    using Clock = std::function<std::int64_t()>;  // Unix milliseconds

    explicit HistoryView(Clock now_ms = {});

    void open(const UndoTree& tree);
    void close();
    bool is_open() const noexcept { return tree_ != nullptr; }
    // The Persist History checkbox in the footer.
    void set_persist(bool on) noexcept { persist_ = on; }

    HistoryKeyResult handle_key(const KeyEvent& key);
    // `focused`: the pane has the keys (not the previewed text, after Tab).
    void render(Screen& screen, Rect area, bool focused = true);

    const std::vector<HistoryRow>& rows() const noexcept { return rows_; }
    std::size_t selected() const noexcept { return selected_; }
    // The node of the selected row, for the preview; none on a read-only row.
    std::optional<NodeId> selected_node() const {
        if (selected_ >= rows_.size() || rows_[selected_].read_only) return std::nullopt;
        return rows_[selected_].node;
    }
    std::string row_text(std::size_t index) const;

private:
    void rebuild();  // the rows from the open branches, keeping the selected node
    void emit_tree(NodeId root, bool read_only);
    void select_node(NodeId node);
    void keep_visible();
    std::int64_t now() const;

    Clock now_ms_;
    const UndoTree* tree_ = nullptr;  // while open
    std::vector<HistoryRow> rows_;
    std::vector<NodeId> roots_;               // the trees, top to bottom: older ones, then the current one
    std::unordered_set<NodeId> open_;         // changes whose side branches are shown
    std::unordered_set<NodeId> open_trees_;   // older trees shown in full (by root)
    std::optional<NodeId> current_root_;      // the current state's tree, always shown in full
    std::unordered_set<NodeId> redo_path_;    // the current state's preferred descendants
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
    int view_rows_ = 20;  // the height of the last rendered area
    bool persist_ = false;
};

}  // namespace mod
