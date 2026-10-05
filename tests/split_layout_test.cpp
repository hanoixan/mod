#include <doctest/doctest.h>

#include <utility>
#include <vector>

#include "app/split_layout.hpp"

using namespace mod;

TEST_CASE("max_splits: three rows each, at least one") {
    CHECK(max_splits(24) == 8);
    CHECK(max_splits(25) == 8);
    CHECK(max_splits(3) == 1);
    CHECK(max_splits(2) == 1);
    CHECK(max_splits(0) == 1);
}

TEST_CASE("split_rows covers the rows exactly, evenly, the remainder to the top") {
    using R = std::vector<std::pair<int, int>>;
    CHECK(split_rows(24, 1) == R{{0, 24}});
    CHECK(split_rows(24, 2) == R{{0, 12}, {12, 12}});
    CHECK(split_rows(24, 5) == R{{0, 5}, {5, 5}, {10, 5}, {15, 5}, {20, 4}});
    CHECK(split_rows(10, 3) == R{{0, 4}, {4, 3}, {7, 3}});
    for (int rows : {9, 24, 51}) {
        for (int n = 1; n <= max_splits(rows); ++n) {
            const auto r = split_rows(rows, n);
            REQUIRE(r.size() == static_cast<std::size_t>(n));
            int top = 0;
            for (const auto& [t, h] : r) {
                CHECK(t == top);
                CHECK(h >= 3);
                top += h;
            }
            CHECK(top == rows);
        }
    }
}

TEST_CASE("focus_after_unsplit: the split above, else the one that was below") {
    CHECK(focus_after_unsplit(0) == 0);
    CHECK(focus_after_unsplit(1) == 0);
    CHECK(focus_after_unsplit(2) == 1);
    CHECK(focus_after_unsplit(0) == 0);
}
