#pragma once

#include <cstdint>
#include <optional>

#include "app/document_slot.hpp"
#include "edit/undo_tree.hpp"
#include "ui/input.hpp"
#include "util/error.hpp"

namespace mod {

// The Undo History pane's preview, in the document's own view: the text of the node the pane
// selects, read-only, at the lines the view showed when the pane opened; scrolled by the
// motions while it has the focus; and put back exactly as it was when the pane closes.
// Main thread.
class HistoryPreview {
public:
    // The pane opens on `slot`: its place is remembered, its highlighter set aside (its
    // cached state would not match the previewed text), and `node` shown.
    Status begin(DocumentSlot& slot, std::optional<NodeId> node, int rows, int cols);
    // Shows `node`'s text, or the current state's for a row without a node, in a text area
    // of `rows` × `cols`: scrolled the least that shows the step's change (its last mark)
    // kChangeMargin rows clear of the top and the bottom; a step without a change is shown
    // at the lines of the remembered place. On an error the view shows the text without
    // its marks.
    Status show(DocumentSlot& slot, std::optional<NodeId> node, int rows, int cols);
    static constexpr int kChangeMargin = 2;
    // The document's text, the view's place and its highlighter, as they were; the focus
    // goes back to the pane.
    void end(DocumentSlot& slot);
    // With the text focused, Up, Down, Page Up, Page Down, Home and End scroll it, a page
    // being `page_rows` lines; true when `key` was one of them.
    bool scroll(DocumentSlot& slot, const KeyEvent& key, int page_rows);

    bool text_focused() const noexcept { return text_focus_; }
    void toggle_focus() noexcept { text_focus_ = !text_focus_; }

private:
    std::uint64_t top_ = 0;
    std::optional<std::uint64_t> top_line_;
    bool text_focus_ = false;
};

}  // namespace mod
