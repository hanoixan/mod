#include "app/document_list.hpp"

#include <algorithm>

namespace mod {

DocumentList::Id DocumentList::add() {
    const Id id = next_++;
    order_.push_back(id);
    recent_.push_back(id);
    return id;
}

void DocumentList::show(Id id) {
    if (!contains(id)) return;
    std::erase(recent_, id);
    recent_.push_back(id);
}

std::optional<DocumentList::Id> DocumentList::remove(Id id) {
    std::erase(order_, id);
    std::erase(recent_, id);
    return shown();
}

std::optional<DocumentList::Id> DocumentList::shown() const {
    if (recent_.empty()) return std::nullopt;
    return recent_.back();
}

bool DocumentList::contains(Id id) const { return std::ranges::find(order_, id) != order_.end(); }

}  // namespace mod
