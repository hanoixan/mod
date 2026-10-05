#pragma once

#include <chrono>
#include <memory>
#include <optional>

#include "edit/document.hpp"
#include "edit/editor.hpp"
#include "search/search.hpp"
#include "syntax/highlight.hpp"
#include "ui/editor_view.hpp"

namespace mod {

// A document's View options, shared by every slot that views it so its split views agree.
struct ViewOptions {
    bool line_numbers = true;
    bool syntax = true;
    bool word_wrap = true;
    bool read_only = false;
};

// One shown document and the components built on it: the editor, the searcher, the
// view and the highlighter. Moved as a whole when the screen shows another document.
// Main thread.
struct DocumentSlot {
    // Shared by every slot (split view) that shows the document; the rest is this view's own.
    std::shared_ptr<Document> doc;
    std::unique_ptr<Editor> editor;
    std::unique_ptr<Searcher> searcher;
    std::unique_ptr<EditorView> view;
    std::shared_ptr<Highlighter> highlighter;
    std::shared_ptr<ViewOptions> options;  // the document's, shared like `doc`
    std::optional<std::chrono::steady_clock::time_point> highlighter_deadline;

    DocumentSlot() = default;
    DocumentSlot(DocumentSlot&&) noexcept = default;
    DocumentSlot& operator=(DocumentSlot&&) noexcept;
    ~DocumentSlot();

    // Tears down in dependency order: the highlighter and the view, then the searcher and
    // the editor, then the document.
    void reset();
    // Drops this slot's share of the highlighter; the last share leaves the document's listeners.
    void release_highlighter();
};

}  // namespace mod
