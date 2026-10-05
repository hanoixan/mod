#include "app/split_layout.hpp"

#include <algorithm>

namespace mod {

int max_splits(int rows) { return std::max(1, rows / 3); }

std::vector<std::pair<int, int>> split_rows(int rows, int count) {
    count = std::max(1, count);
    std::vector<std::pair<int, int>> out;
    const int base = rows / count;
    const int extra = rows % count;
    int top = 0;
    for (int i = 0; i < count; ++i) {
        const int h = base + (i < extra ? 1 : 0);
        out.emplace_back(top, h);
        top += h;
    }
    return out;
}

std::size_t focus_after_unsplit(std::size_t focus) { return focus > 0 ? focus - 1 : 0; }

}  // namespace mod
