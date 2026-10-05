#pragma once

#include <cstddef>
#include <optional>

#include "ui/input.hpp"

namespace mod {

// The rows Page Up and Page Down move in the panels' lists.
inline constexpr std::size_t kListPage = 10;

// Where a list key takes the selection, and which way (-1 up, 1 down) to look from there
// for a row that can be selected, in a list whose headers cannot.
struct ListStep {
    std::size_t index;
    int direction;
};

// Up and Down move one row, Page Up and Page Down kListPage, Home and End to the ends, never
// past either; nullopt for any other key. An empty list lands on row 0.
std::optional<ListStep> list_step(Key key, std::size_t selected, std::size_t count);

// The first row of a window of `visible` rows (at least 1) that shows row `selected`,
// moved from `scroll` only as far as it must.
std::size_t scroll_to_show(std::size_t selected, std::size_t scroll, std::size_t visible);

}  // namespace mod
