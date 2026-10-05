#include "app/document_slot.hpp"

namespace mod {

DocumentSlot& DocumentSlot::operator=(DocumentSlot&& other) noexcept {
    if (this != &other) {
        reset();
        doc = std::move(other.doc);
        editor = std::move(other.editor);
        searcher = std::move(other.searcher);
        view = std::move(other.view);
        highlighter = std::move(other.highlighter);
        highlighter_deadline = other.highlighter_deadline;
        options = std::move(other.options);
    }
    return *this;
}

DocumentSlot::~DocumentSlot() { reset(); }

void DocumentSlot::release_highlighter() {
    if (!highlighter) return;
    if (view) view->set_highlighter(nullptr);
    if (doc && highlighter.use_count() == 1) doc->remove_listener(highlighter.get());
    highlighter.reset();
}

void DocumentSlot::reset() {
    release_highlighter();
    highlighter_deadline.reset();
    view.reset();
    searcher.reset();
    editor.reset();
    doc.reset();
    options.reset();
}

}  // namespace mod
