#include <doctest/doctest.h>

#include <chrono>
#include <string>

#include "app/notices.hpp"

using namespace mod;
using namespace std::chrono_literals;

TEST_CASE("a notice shows until cleared; a timed one ends that long after it is first shown") {
    Notices n;
    const int a = 0;
    const int b = 0;
    const auto t0 = Notices::Clock::now();
    CHECK_FALSE(n.show(&a, t0));
    n.post(&a, "changed on disk");
    CHECK(n.show(&a, t0) == "changed on disk");
    CHECK(n.show(&a, t0 + 1h) == "changed on disk");  // no lifetime: it stays
    CHECK_FALSE(n.next_deadline());
    // A timed notice waits for its document to be shown.
    n.post(&b, "reloaded", 5s);
    CHECK_FALSE(n.next_deadline());
    CHECK(n.show(&b, t0 + 10s) == "reloaded");
    CHECK(n.next_deadline() == t0 + 15s);
    CHECK(n.show(&b, t0 + 14s) == "reloaded");
    CHECK_FALSE(n.show(&b, t0 + 15s));
    CHECK_FALSE(n.has(&b));
    // Posting again replaces it; clear and retain drop it.
    n.post(&a, "deleted on disk");
    CHECK(n.show(&a, t0) == "deleted on disk");
    REQUIRE(n.posted(&a));
    n.retain([&](const void* key) { return key != &a; });
    CHECK_FALSE(n.has(&a));
    n.post(&a, "x");
    n.clear(&a);
    CHECK_FALSE(n.posted(&a));
}
