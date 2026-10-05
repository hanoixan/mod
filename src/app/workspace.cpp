#include "app/workspace.hpp"

#include "platform/fs.hpp"

#include <algorithm>
#include <cassert>

#include "app/split_layout.hpp"

namespace mod {

Workspace::Workspace() : views_(1) {}

const Document* Workspace::document_of(const View& v) { return v.ro.away ? v.ro.base.doc.get() : v.slot.doc.get(); }

std::optional<std::size_t> Workspace::other_view_of(DocumentList::Id id) const {
    for (std::size_t i = 0; i < views_.size(); ++i)
        if (i != focus_ && views_[i].id == id && id != 0) return i;
    return std::nullopt;
}

const View* Workspace::entry_of(DocumentList::Id id) const {
    if (id == 0) return nullptr;
    if (views_[focus_].id == id) return &views_[focus_];
    if (const auto i = other_view_of(id)) return &views_[*i];
    const auto it = parked_.find(id);
    return it == parked_.end() ? nullptr : &it->second;
}

std::optional<DocumentList::Id> Workspace::find(const std::filesystem::path& path) const {
    // Resolved as Document resolves the path it opens; a hard link has another name, so
    // after the names the files are compared.
    const auto real = resolve_real_path(path);
    const auto wanted = stat_path(path);
    for (const DocumentList::Id id : docs_.order()) {
        const View* v = entry_of(id);
        const Document* d = v != nullptr ? document_of(*v) : nullptr;
        if (d == nullptr || d->is_untitled()) continue;
        if (real && d->path() == *real) return id;
        if (wanted) {
            const auto open = stat_path(d->path());
            if (open && open->device == wanted->device && open->inode == wanted->inode) return id;
        }
    }
    return std::nullopt;
}

void Workspace::release_focused() {
    View& v = views_[focus_];
    if (v.id != 0 && !other_view_of(v.id)) parked_[v.id] = std::move(v);
    v = View{};
}

bool Workspace::take_parked(DocumentList::Id id) {
    const auto it = parked_.find(id);
    if (it == parked_.end()) return false;
    assert(views_[focus_].id == 0);
    views_[focus_] = std::move(it->second);
    parked_.erase(it);
    docs_.show(id);
    return true;
}

void Workspace::focus_on(std::size_t index) {
    focus_ = index;
    if (views_[index].id != 0) docs_.show(views_[index].id);
}

void Workspace::insert_below_focus(View v) { views_.insert(views_.begin() + static_cast<std::ptrdiff_t>(focus_) + 1, std::move(v)); }

void Workspace::remove_focused() {
    assert(views_.size() > 1);
    const std::size_t next = focus_after_unsplit(focus_);
    views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(focus_));
    focus_on(next);
}

void Workspace::remove_other_views_of(DocumentList::Id id) {
    for (std::size_t i = views_.size(); i-- > 0;) {
        if (i == focus_ || views_[i].id != id) continue;
        views_.erase(views_.begin() + static_cast<std::ptrdiff_t>(i));
        if (i < focus_) --focus_;
    }
}

void Workspace::trim(std::size_t max) {
    while (views_.size() > std::max<std::size_t>(1, max)) {
        const std::size_t last = views_.size() - 1;
        if (focus_ == last) focus_on(last - 1);
        View gone = std::move(views_[last]);
        views_.pop_back();
        bool shown = false;
        for (const View& v : views_) shown = shown || v.id == gone.id;
        if (gone.id != 0 && !shown) parked_[gone.id] = std::move(gone);
    }
}

void Workspace::clear() {
    parked_.clear();
    views_.clear();
    views_.resize(1);
    focus_ = 0;
}

std::vector<DocumentList::Id> Workspace::unsaved() const {
    std::vector<DocumentList::Id> out;
    for (const DocumentList::Id id : docs_.order()) {
        const View* v = entry_of(id);
        const Document* d = v != nullptr ? document_of(*v) : nullptr;
        if (d != nullptr && d->is_dirty()) out.push_back(id);
    }
    return out;
}

}  // namespace mod
