#include <doctest/doctest.h>

#include <vector>

#include "app/document_list.hpp"

using namespace mod;
using Ids = std::vector<DocumentList::Id>;

TEST_CASE("documents keep their opening order; the newest is shown") {
    DocumentList l;
    CHECK_FALSE(l.shown().has_value());
    const auto a = l.add();
    const auto b = l.add();
    const auto c = l.add();
    CHECK(a != b);
    CHECK(b != c);
    CHECK(l.order() == Ids{a, b, c});
    CHECK(l.shown() == c);
    l.show(a);
    CHECK(l.shown() == a);
    CHECK(l.order() == Ids{a, b, c});  // showing never reorders the menu
    CHECK(l.contains(b));
    CHECK_FALSE(l.contains(99));
}

TEST_CASE("closing the shown document shows the one viewed most recently before it") {
    DocumentList l;
    const auto a = l.add();
    const auto b = l.add();
    const auto c = l.add();
    l.show(a);
    l.show(c);
    l.show(b);  // viewed: a, c, b
    CHECK(l.remove(b) == c);
    CHECK(l.shown() == c);
    CHECK(l.order() == Ids{a, c});
    CHECK(l.remove(c) == a);
    CHECK(l.remove(a) == std::nullopt);
    CHECK(l.size() == 0);
    CHECK_FALSE(l.shown().has_value());
}

TEST_CASE("removing a document that is not shown keeps the shown one") {
    DocumentList l;
    const auto a = l.add();
    const auto b = l.add();
    CHECK(l.remove(a) == b);
    CHECK(l.order() == Ids{b});
    CHECK(l.remove(42) == b);  // unknown: nothing changes
    const auto c = l.add();
    CHECK(c != a);  // ids are never reused
}
