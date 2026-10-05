#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
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

// One node row, or a connector row (no `node`) closing lanes into a parent's lane.
struct HistoryRow {
    std::optional<NodeId> node;
    std::string graph;
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

// The undo-history panel: the whole forest as a `git log --graph`-style list. Main thread.
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
    void render(Screen& screen, Rect area);

    const std::vector<HistoryRow>& rows() const noexcept { return rows_; }
    std::size_t selected() const noexcept { return selected_; }
    // The node of the selected row, for the preview; none on a connector or read-only row.
    std::optional<NodeId> selected_node() const {
        if (selected_ >= rows_.size() || rows_[selected_].read_only) return std::nullopt;
        return rows_[selected_].node;
    }
    std::string row_text(std::size_t index) const;

private:
    void build_tree(NodeId root, bool read_only);
    void select(std::size_t index, int direction);
    void keep_visible();
    std::int64_t now() const;

    Clock now_ms_;
    const UndoTree* tree_ = nullptr;  // while open
    std::vector<HistoryRow> rows_;
    std::size_t selected_ = 0;
    std::size_t scroll_ = 0;
    int view_rows_ = 20;  // the height of the last rendered area
    bool persist_ = false;
};

}  // namespace mod
