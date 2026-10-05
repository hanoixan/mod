// Random editing sessions on a real Document, checked against a model: every node of the
// history must give back exactly the text it was left with, through undo, redo, branch
// cycling and jumps, and again after the session is written to its sidecar and reopened.
#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <thread>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <random>
#include <string>

#include "edit/document.hpp"
#include "edit/sidecar.hpp"
#include "util/event_queue.hpp"

using namespace mod;
namespace fs = std::filesystem;

namespace {

fs::path scratch(const std::string& name) {
    const fs::path dir = fs::path(MOD_TEST_SCRATCH) / "history_model_test";
    fs::create_directories(dir);
    const fs::path p = dir / name;
    fs::remove(p);
    fs::remove(sidecar_path_for(p));
    return p;
}

void write_file(const fs::path& p, std::string_view bytes) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string read_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

std::string text_of(const Document& d) { return d.text().read(0, d.text().size()); }

// A reopened history is verified against the file in the background; wait for it.
void settle(Document& d, EventQueue& q) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(20);
    while (d.history_state() == HistoryState::verifying) {
        if (q.drain() == 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
        REQUIRE(std::chrono::steady_clock::now() < deadline);
    }
}

DocumentOptions persistent() {
    DocumentOptions o;
    o.persist_history = PersistHistory::always;
    return o;
}

// The model: the text each node of the history stands for.
struct Model {
    std::map<NodeId, std::string> text;

    // After any operation: where the history now is must match what the model knows of
    // that node, and a node seen for the first time (or amended by typing) is recorded.
    void observe(const Document& d, bool may_change) {
        const auto at = d.history().current();
        REQUIRE(at.has_value());
        const auto it = text.find(*at);
        if (it == text.end() || may_change) {
            text[*at] = text_of(d);
        } else {
            REQUIRE(text_of(d) == it->second);
        }
    }
};

struct Session {
    std::mt19937_64 rng;
    explicit Session(std::uint64_t seed) : rng(seed) {}
    std::uint64_t below(std::uint64_t n) { return n == 0 ? 0 : rng() % n; }

    std::string some_text() {
        static constexpr std::string_view kAlphabet = "ab \n\xC3\xA9\t";
        std::string s;
        const auto len = 1 + below(below(5) == 0 ? 40 : 4);
        while (s.size() < len) s += kAlphabet[below(kAlphabet.size())];
        return s;
    }

    // One random step: an edit of some kind, or a move through the history.
    void step(Document& d, Model& m) {
        const std::uint64_t size = d.text().size();
        switch (below(10)) {
            case 0:
            case 1:
            case 2: {  // typing, which coalesces into the open node
                const auto at = below(size + 1);
                const std::string t = some_text();
                d.apply(at, 0, std::string_view(t), EditKind::typing, at, at + t.size());
                m.observe(d, true);
                break;
            }
            case 3: {  // a deletion
                if (size == 0) break;
                const auto at = below(size);
                const auto len = 1 + below(std::min<std::uint64_t>(size - at, 30));
                d.apply(at, len, std::string_view(), EditKind::delete_, at + len, at);
                m.observe(d, true);
                break;
            }
            case 4: {  // a replacement, its own node
                const auto at = below(size + 1);
                const auto len = below(std::min<std::uint64_t>(size - at, 10) + 1);
                const std::string t = some_text();
                d.apply(at, len, std::string_view(t), EditKind::paste, at, at + t.size());
                m.observe(d, true);
                break;
            }
            case 5:
            case 6:
                (void)d.undo();
                m.observe(d, false);
                break;
            case 7:
                (void)d.redo();
                m.observe(d, false);
                break;
            case 8:
                (void)d.cycle_branch(below(2) == 0 ? 1 : -1);
                m.observe(d, false);
                break;
            default: {  // a jump to any node seen so far
                auto it = m.text.begin();
                std::advance(it, static_cast<std::ptrdiff_t>(below(m.text.size())));
                REQUIRE(d.jump_to(it->first));
                REQUIRE(text_of(d) == it->second);
                break;
            }
        }
    }
};

}  // namespace

TEST_CASE("every node of a random history gives back its text, before and after a reopen") {
    for (const std::uint64_t seed : {1u, 2u, 3u, 4u, 5u}) {
        CAPTURE(seed);
        const fs::path p = scratch("session" + std::to_string(seed) + ".txt");
        write_file(p, "first line\nsecond line\n");
        EventQueue q({});
        Model m;
        Session s(seed);
        std::string saved_text;
        {
            auto d = Document::open(p, q, persistent());
            REQUIRE(d);
            m.observe(**d, true);
            for (int i = 0; i < 1500; ++i) {
                s.step(**d, m);
                if (i % 250 == 249) {
                    REQUIRE((*d)->save());
                    saved_text = text_of(**d);
                }
            }
            REQUIRE((*d)->save());
            saved_text = text_of(**d);
        }
        REQUIRE(read_file(p) == saved_text);

        auto d = Document::open(p, q, persistent());
        REQUIRE(d);
        settle(**d, q);
        CHECK((*d)->history_state() == HistoryState::attached);
        CHECK(text_of(**d) == saved_text);
        for (const auto& [node, text] : m.text) {
            CAPTURE(node);
            REQUIRE((*d)->history().contains(node));
            const auto jumped = (*d)->jump_to(node);
            if (!jumped) FAIL_CHECK(jumped.error().message);
            REQUIRE(jumped);
            CHECK(text_of(**d) == text);
        }
    }
}

TEST_CASE("a sidecar cut short at any byte, or with any byte flipped, opens safely") {
    const fs::path p = scratch("cut.txt");
    write_file(p, "alpha\nbeta\n");
    EventQueue q({});
    Model m;
    Session s(99);
    {
        auto d = Document::open(p, q, persistent());
        REQUIRE(d);
        m.observe(**d, true);
        for (int i = 0; i < 60; ++i) s.step(**d, m);
        REQUIRE((*d)->save());
    }
    const std::string file = read_file(p);
    const std::string good = read_file(sidecar_path_for(p));
    REQUIRE(good.size() > 64);

    const auto check_opens = [&] {
        auto d = Document::open(p, q, persistent());
        REQUIRE(d);
        settle(**d, q);
        CHECK(text_of(**d) == file);  // the file itself is never affected
        // A history recovered from what is left must agree with the model wherever it
        // reaches; a fresh one (its root is the file as it is now) has nothing to compare.
        const auto root = (*d)->history().roots();
        if (root.empty() || !(*d)->jump_to(root.front()) || text_of(**d) != m.text.begin()->second) return;
        for (const auto& [node, text] : m.text) {
            if (!(*d)->history().contains(node) || !(*d)->jump_to(node)) continue;
            CHECK(text_of(**d) == text);
        }
    };
    for (std::size_t cut = 0; cut < good.size(); cut += (good.size() > 400 ? 3 : 1)) {
        CAPTURE(cut);
        write_file(sidecar_path_for(p), good.substr(0, cut));
        check_opens();
    }
    std::mt19937_64 rng(7);
    for (int i = 0; i < 300; ++i) {
        std::string bad = good;
        const auto at = rng() % bad.size();
        bad[at] = static_cast<char>(bad[at] ^ static_cast<char>(1 + rng() % 255));
        CAPTURE(at);
        write_file(sidecar_path_for(p), bad);
        check_opens();
    }
}
