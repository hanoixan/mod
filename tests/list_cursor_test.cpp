#include <doctest/doctest.h>

#include "ui/list_cursor.hpp"

using namespace mod;

namespace {

std::size_t to(Key key, std::size_t selected, std::size_t count) {
    const auto step = list_step(key, selected, count);
    REQUIRE(step.has_value());
    return step->index;
}

}  // namespace

TEST_CASE("list_step: one row, a page, or an end, never past either end") {
    CHECK(to(Key::Down, 3, 50) == 4);
    CHECK(to(Key::Up, 3, 50) == 2);
    CHECK(to(Key::Up, 0, 50) == 0);
    CHECK(to(Key::Down, 49, 50) == 49);
    CHECK(to(Key::PageDown, 3, 50) == 3 + kListPage);
    CHECK(to(Key::PageDown, 45, 50) == 49);
    CHECK(to(Key::PageUp, 15, 50) == 15 - kListPage);
    CHECK(to(Key::PageUp, 4, 50) == 0);
    CHECK(to(Key::Home, 30, 50) == 0);
    CHECK(to(Key::End, 3, 50) == 49);
    CHECK(to(Key::End, 0, 0) == 0);  // an empty list: row 0, which the caller checks
    CHECK_FALSE(list_step(Key::Enter, 3, 50).has_value());
    CHECK_FALSE(list_step(Key::Char, 3, 50).has_value());
}

TEST_CASE("list_step: the direction to look for a selectable row from where it lands") {
    CHECK(list_step(Key::Down, 3, 50)->direction == 1);
    CHECK(list_step(Key::PageDown, 3, 50)->direction == 1);
    CHECK(list_step(Key::Home, 3, 50)->direction == 1);
    CHECK(list_step(Key::Up, 3, 50)->direction == -1);
    CHECK(list_step(Key::PageUp, 3, 50)->direction == -1);
    CHECK(list_step(Key::End, 3, 50)->direction == -1);
}

TEST_CASE("scroll_to_show: the window moves only as far as it must") {
    CHECK(scroll_to_show(5, 0, 10) == 0);    // already shown
    CHECK(scroll_to_show(12, 0, 10) == 3);   // below: the selection becomes the last row
    CHECK(scroll_to_show(2, 6, 10) == 2);    // above: the selection becomes the first row
    CHECK(scroll_to_show(9, 0, 10) == 0);
    CHECK(scroll_to_show(10, 0, 10) == 1);
    CHECK(scroll_to_show(7, 7, 1) == 7);
}
