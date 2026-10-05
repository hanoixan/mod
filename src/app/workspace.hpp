#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <utility>
#include <vector>

#include "app/document_list.hpp"
#include "app/document_slot.hpp"
#include "app/read_only.hpp"
#include "syntax/markdown.hpp"

namespace mod {

// A view's read-only mode: where it has been, and, while it shows a followed link's target,
// the document itself waiting in `base`.
struct ReadOnlyState {
    ReadOnlyNav nav;
    bool away = false;
    DocumentSlot base;
    const Document* outline_doc = nullptr;  // `outline` is for this document at this version
    std::uint64_t outline_version = 0;
    MarkdownOutline outline;
};

// One split view, or a parked document: which open document (`id`, 0 for none), its slot,
// and its read-only state.
struct View {
    DocumentList::Id id = 0;
    DocumentSlot slot;
    ReadOnlyState ro;
};

// The open documents and the split views onto them. A document is in one or more views or
// parked; views and parked documents are told apart by document id, never by the slot's
// document (which, while a view follows a link, is the link's target). Main thread.
class Workspace {
public:
    Workspace();  // one view, showing nothing yet

    DocumentList& documents() noexcept { return docs_; }
    const DocumentList& documents() const noexcept { return docs_; }

    std::size_t size() const noexcept { return views_.size(); }
    std::size_t focus() const noexcept { return focus_; }
    View& focused() { return views_[focus_]; }
    const View& focused() const { return views_[focus_]; }
    View& at(std::size_t i) { return views_[i]; }
    const View& at(std::size_t i) const { return views_[i]; }

    // The document a view or parked entry stands for: behind a followed link, the base.
    static const Document* document_of(const View& v);

    // A view other than the focused one that shows document `id`.
    std::optional<std::size_t> other_view_of(DocumentList::Id id) const;
    // The open document whose file is `path`, by any name (relative, through symlinks, or a
    // hard link), wherever it is.
    std::optional<DocumentList::Id> find(const std::filesystem::path& path) const;
    // Every open document's view or parked entry, focused view first, then the other views
    // top to bottom, then the parked documents; nullptr for an id not open.
    const View* entry_of(DocumentList::Id id) const;
    View* entry_of(DocumentList::Id id) { return const_cast<View*>(std::as_const(*this).entry_of(id)); }

    // The focused view lets go of its document, leaving it empty: dropped when another view
    // still shows it, parked otherwise.
    void release_focused();
    // Moves parked document `id` into the focused view, which must be empty.
    bool take_parked(DocumentList::Id id);
    // Moves the focus to view `index`; the document list learns which is shown.
    void focus_on(std::size_t index);
    void insert_below_focus(View v);
    // Removes the focused view (already released): the focus goes to the view above, else
    // to the one below.
    void remove_focused();
    // Removes every view but the focused one that shows document `id` (it is being closed).
    void remove_other_views_of(DocumentList::Id id);
    // Removes bottom views until at most `max` are left (the focus moving up out of them),
    // parking a removed view's document unless another view still shows it.
    void trim(std::size_t max);
    // The documents with unsaved changes, in opening order.
    std::vector<DocumentList::Id> unsaved() const;
    // Closes everything: the parked documents, then the views (each slot tearing down in
    // dependency order). One empty view is left.
    void clear();

    // `fn` on every view's slot.
    template <class Fn>
    void for_each_view(Fn fn) {
        for (View& v : views_)
            if (v.slot.view) fn(v.slot);
    }
    // `fn` on every slot of every open document: the views', the documents waiting behind
    // a followed link, and the parked documents'.
    template <class Fn>
    void for_each_slot(Fn fn) {
        const auto visit = [&](View& v) {
            if (v.slot.doc) fn(v.slot);
            if (v.ro.away && v.ro.base.doc) fn(v.ro.base);
        };
        for (View& v : views_) visit(v);
        for (auto& [id, v] : parked_) visit(v);
    }

private:
    DocumentList docs_;
    std::vector<View> views_;
    std::size_t focus_ = 0;
    std::map<DocumentList::Id, View> parked_;
};

}  // namespace mod
