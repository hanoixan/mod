#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace mod {

// The open documents as ids: the order they were opened in (the Documents menu) and the
// order they were last viewed in (what Close shows next). Main thread.
class DocumentList {
public:
    using Id = std::uint64_t;

    // A new document, now the shown one.
    Id add();
    void show(Id id);
    // Removes `id`; the shown document is then the most recently viewed of the rest, or
    // none when the list is empty.
    std::optional<Id> remove(Id id);

    std::optional<Id> shown() const;
    const std::vector<Id>& order() const noexcept { return order_; }
    std::size_t size() const noexcept { return order_.size(); }
    bool contains(Id id) const;

private:
    std::vector<Id> order_;
    std::vector<Id> recent_;  // most recently viewed last
    Id next_ = 1;
};

}  // namespace mod
