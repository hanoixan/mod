#include "app/history_preview.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace mod {

Status HistoryPreview::begin(DocumentSlot& slot, std::optional<NodeId> node, int rows, int cols) {
    text_focus_ = false;
    top_ = slot.view->top();
    top_line_ = slot.doc->line_of(top_, false);
    slot.view->set_highlighter(nullptr);
    return show(slot, node, rows, cols);
}

Status HistoryPreview::show(DocumentSlot& slot, std::optional<NodeId> node, int rows, int cols) {
    Document& doc = *slot.doc;
    if (!node) node = doc.history().current();
    if (!node) return {};
    auto marks = doc.begin_preview(*node);
    const std::optional<std::uint64_t> change = marks && !marks->empty() ? std::optional(marks->back().start) : std::nullopt;
    slot.view->set_preview(marks ? std::move(*marks) : std::vector<PreviewMark>{});
    if (change) {
        // From what is on screen now, the least move that shows where the step changed the text.
        slot.view->scroll_to(*change, rows, cols, kChangeMargin, true);
    } else {
        // The same lines as when the pane opened.
        std::uint64_t top = std::min(top_, doc.text().size());
        if (top_line_) {
            if (const auto start = doc.line_start(*top_line_, false)) top = std::min(*start, doc.text().size());
        }
        slot.view->set_top(top);
    }
    if (!marks) return std::unexpected(marks.error());
    return {};
}

void HistoryPreview::end(DocumentSlot& slot) {
    if (slot.doc && slot.doc->previewing()) slot.doc->end_preview();
    if (slot.view && slot.view->previewing()) {
        slot.view->set_preview(std::nullopt);
        slot.view->set_top(std::min(top_, slot.doc->text().size()));
        slot.view->set_highlighter(slot.highlighter.get());
    }
    text_focus_ = false;
}

bool HistoryPreview::scroll(DocumentSlot& slot, const KeyEvent& key, int page_rows) {
    const PieceTree& t = slot.doc->text();
    const std::uint64_t size = t.size();
    const int rows = std::max(1, page_rows);
    std::uint64_t top = slot.view->top();
    auto down = [&](int n) {
        for (int i = 0; i < n; ++i) {
            const std::uint64_t lf = top < size ? t.find_lf_forward(top, size - top) : PieceTree::npos;
            if (lf == PieceTree::npos) break;
            top = lf + 1;
        }
    };
    auto up = [&](int n) {
        for (int i = 0; i < n && top > 0; ++i) {
            // `top - 1` is the line feed ending the line above; its start follows the one before.
            const std::uint64_t lf = t.find_lf_backward(top - 1, top - 1);
            top = lf == PieceTree::npos ? 0 : lf + 1;
        }
    };
    switch (key.key) {
        case Key::Down: down(1); break;
        case Key::Up: up(1); break;
        case Key::PageDown: down(rows); break;
        case Key::PageUp: up(rows); break;
        case Key::Home: top = 0; break;
        case Key::End:
            top = size;
            up(rows);
            break;
        default: return false;
    }
    slot.view->set_top(top);
    return true;
}

}  // namespace mod
