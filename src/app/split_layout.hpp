#pragma once

#include <cstddef>
#include <utility>
#include <vector>

namespace mod {

// How many split views a screen of `rows` may hold: three rows each, at least one.
int max_splits(int rows);

// Each of `count` splits' {top, height}, top to bottom, as even as they can be (the
// remainder one row each to the top splits).
std::vector<std::pair<int, int>> split_rows(int rows, int count);

// After removing split `focus`: the index of the split to focus (the one above, else the
// one that was below).
std::size_t focus_after_unsplit(std::size_t focus);

}  // namespace mod
