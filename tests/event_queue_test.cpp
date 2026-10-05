#include <doctest/doctest.h>

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "util/event_queue.hpp"

using namespace mod;

TEST_CASE("tasks run in the order posted, each once, and wake follows every post") {
    int wakes = 0;
    EventQueue q([&] { ++wakes; });
    std::string order;
    q.post([&] { order += 'a'; });
    q.post([&] { order += 'b'; });
    CHECK(wakes == 2);
    CHECK(q.drain() == 2);
    CHECK(order == "ab");
    CHECK(q.drain() == 0);
    CHECK(order == "ab");
}

TEST_CASE("a task posted while draining runs on the next drain") {
    EventQueue q({});
    std::string order;
    q.post([&] {
        order += 'a';
        q.post([&] { order += 'b'; });
    });
    q.drain();
    CHECK(order == "a");
    q.drain();
    CHECK(order == "ab");
}

TEST_CASE("a task that throws loses none of the tasks after it") {
    EventQueue q({});
    std::string order;
    q.post([&] { order += 'a'; });
    q.post([] { throw std::runtime_error("boom"); });
    q.post([&] { order += 'c'; });
    q.post([&] { order += 'd'; });
    CHECK_THROWS_AS(q.drain(), std::runtime_error);
    CHECK(order == "a");
    q.post([&] { order += 'e'; });  // posted after the throw: still after the ones left
    q.drain();
    CHECK(order == "acde");
}

TEST_CASE("a closed queue refuses posts and drops what was pending") {
    EventQueue q({});
    int ran = 0;
    q.post([&] { ++ran; });
    q.close();
    CHECK_FALSE(q.post([&] { ++ran; }));
    CHECK(q.drain() == 0);
    CHECK(ran == 0);
}

TEST_CASE("posts from many threads all arrive") {
    EventQueue q({});
    int ran = 0;
    std::vector<std::jthread> workers;
    for (int t = 0; t < 8; ++t) {
        workers.emplace_back([&] {
            for (int i = 0; i < 1000; ++i) q.post([&] { ++ran; });
        });
    }
    workers.clear();
    q.drain();
    CHECK(ran == 8000);
}
