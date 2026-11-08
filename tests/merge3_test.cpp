#include <doctest/doctest.h>

#include <algorithm>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "edit/merge3.hpp"

using namespace mod;

namespace {

LineIndex index_of(std::string_view text, std::size_t piece = 3) {
    LineIndexer ix;
    for (std::size_t i = 0; i < text.size(); i += piece) {
        const std::string_view p = text.substr(i, piece);
        ix.feed(std::as_bytes(std::span(p.data(), p.size())));
    }
    return ix.finish();
}

// Lines of single letters, one per character: "abc" is three lines a, b, c.
std::vector<std::uint64_t> L(std::string_view letters) {
    std::string text;
    for (char c : letters) text += std::string(1, c) + "\n";
    return index_of(text).hash;
}

std::vector<std::uint64_t> apply(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b, const std::vector<LineEdit>& edits) {
    std::vector<std::uint64_t> out;
    std::size_t at = 0;
    for (const LineEdit& e : edits) {
        out.insert(out.end(), a.begin() + static_cast<std::ptrdiff_t>(at), a.begin() + static_cast<std::ptrdiff_t>(e.a0));
        out.insert(out.end(), b.begin() + static_cast<std::ptrdiff_t>(e.b0), b.begin() + static_cast<std::ptrdiff_t>(e.b1));
        at = e.a1;
    }
    out.insert(out.end(), a.begin() + static_cast<std::ptrdiff_t>(at), a.end());
    return out;
}

std::size_t lcs(const std::vector<std::uint64_t>& a, const std::vector<std::uint64_t>& b) {
    std::vector<std::vector<std::size_t>> t(a.size() + 1, std::vector<std::size_t>(b.size() + 1, 0));
    for (std::size_t i = 1; i <= a.size(); ++i)
        for (std::size_t j = 1; j <= b.size(); ++j) t[i][j] = a[i - 1] == b[j - 1] ? t[i - 1][j - 1] + 1 : std::max(t[i - 1][j], t[i][j - 1]);
    return t[a.size()][b.size()];
}

}  // namespace

TEST_CASE("lines are indexed with their line feeds, the last one without its own") {
    const LineIndex ix = index_of("one\ntwo\nthree");
    REQUIRE(ix.lines() == 3);
    CHECK(ix.start == std::vector<std::uint64_t>{0, 4, 8, 13});
    CHECK(index_of("one\n", 1).hash[0] == index_of("one\nx", 2).hash[0]);  // the same line, fed in other pieces
    CHECK(index_of("x").hash[0] != index_of("x\n").hash[0]);              // without its line feed it differs
    CHECK(index_of("").lines() == 0);
}

TEST_CASE("diff_lines: edits that rebuild b from a, and no more of them than needed") {
    CHECK(diff_lines(L("abc"), L("abc"))->empty());
    CHECK(*diff_lines(L("abc"), L("axc")) == std::vector<LineEdit>{{1, 2, 1, 2}});
    CHECK(*diff_lines(L(""), L("ab")) == std::vector<LineEdit>{{0, 0, 0, 2}});
    CHECK(*diff_lines(L("ab"), L("")) == std::vector<LineEdit>{{0, 2, 0, 0}});
    std::mt19937 rng(7);
    for (int round = 0; round < 400; ++round) {
        std::string x, y;
        const int nx = static_cast<int>(rng() % 14), ny = static_cast<int>(rng() % 14);
        for (int i = 0; i < nx; ++i) x += static_cast<char>('a' + rng() % 4);
        for (int i = 0; i < ny; ++i) y += static_cast<char>('a' + rng() % 4);
        const auto a = L(x), b = L(y);
        std::atomic<std::size_t> settled{0};
        const auto e = diff_lines(a, b, {}, &settled);
        REQUIRE(e);
        CAPTURE(x);
        CAPTURE(y);
        CHECK(apply(a, b, *e) == b);
        std::size_t removed = 0, inserted = 0;
        for (const LineEdit& d : *e) {
            removed += d.a1 - d.a0;
            inserted += d.b1 - d.b0;
        }
        CHECK(removed + inserted == a.size() + b.size() - 2 * lcs(a, b));  // minimal
        CHECK(settled.load() == a.size());
    }
}

TEST_CASE("diff_lines stops when asked") {
    std::stop_source s;
    s.request_stop();
    std::vector<std::uint64_t> a(5000), b(5000);
    for (std::size_t i = 0; i < a.size(); ++i) {
        a[i] = i;
        b[i] = i + 100000;
    }
    CHECK_FALSE(diff_lines(a, b, s.get_token()));
}

TEST_CASE("merge3: one side's changes are taken, the same change once, overlaps conflict") {
    using H = MergeHunk;
    // Theirs alone: taken.
    CHECK(*merge3(L("abcd"), L("abcd"), L("aXcd")) == std::vector<H>{{H::take_theirs, 1, 2, 1, 2, 1, 2}});
    // Ours alone: nothing to do.
    CHECK(merge3(L("abcd"), L("aYcd"), L("abcd"))->empty());
    // Different places: theirs taken, at ours' coordinates (ours grew by two lines before it).
    CHECK(*merge3(L("abcde"), L("aYYbcde"), L("abcdZ")) == std::vector<H>{{H::take_theirs, 4, 5, 6, 7, 4, 5}});
    // The same change on both sides: nothing.
    CHECK(merge3(L("abc"), L("aXc"), L("aXc"))->empty());
    // Overlapping: a conflict.
    CHECK(*merge3(L("abc"), L("aYc"), L("aXc")) == std::vector<H>{{H::conflict, 1, 2, 1, 2, 1, 2}});
    // Both insert at the same place: a conflict.
    CHECK(*merge3(L("ab"), L("aYb"), L("aXb")) == std::vector<H>{{H::conflict, 1, 1, 1, 2, 1, 2}});
    // Adjacent changes of different lines: both taken, no conflict.
    CHECK(*merge3(L("abcd"), L("aYcd"), L("abXd")) == std::vector<H>{{H::take_theirs, 2, 3, 2, 3, 2, 3}});
}

TEST_CASE("merge3: applying the hunks gives ours with theirs' changes, for random edits that never overlap") {
    std::mt19937 rng(11);
    for (int round = 0; round < 300; ++round) {
        // Base of distinct lines; ours edits the first half, theirs the second.
        std::vector<std::uint64_t> base;
        const std::size_t n = 2 + rng() % 20;
        for (std::size_t i = 0; i < n; ++i) base.push_back(i);
        auto edit = [&](std::size_t from, std::size_t to, std::uint64_t tag) {
            std::vector<std::uint64_t> v;
            for (std::size_t i = 0; i < base.size(); ++i) {
                if (i >= from && i < to && rng() % 3 == 0) {
                    if (rng() % 2) v.push_back(tag + i);  // changed
                    continue;                              // or removed
                }
                v.push_back(base[i]);
                if (i >= from && i < to && rng() % 5 == 0) v.push_back(tag + 500 + i);  // inserted after
            }
            return v;
        };
        const std::size_t mid = n / 2;
        const auto ours = edit(0, mid - (mid > 0 ? 1 : 0), 1000);
        const auto theirs = edit(mid + 1, n, 2000);
        const auto hunks = merge3(base, ours, theirs);
        REQUIRE(hunks);
        std::vector<std::uint64_t> merged;
        std::size_t at = 0;
        for (const MergeHunk& h : *hunks) {
            CHECK(h.kind == MergeHunk::take_theirs);
            merged.insert(merged.end(), ours.begin() + static_cast<std::ptrdiff_t>(at), ours.begin() + static_cast<std::ptrdiff_t>(h.ours0));
            merged.insert(merged.end(), theirs.begin() + static_cast<std::ptrdiff_t>(h.theirs0), theirs.begin() + static_cast<std::ptrdiff_t>(h.theirs1));
            at = h.ours1;
        }
        merged.insert(merged.end(), ours.begin() + static_cast<std::ptrdiff_t>(at), ours.end());
        // The expected result: ours up to the untouched base line `mid`, theirs after it.
        const auto split = [&](const std::vector<std::uint64_t>& v) { return std::find(v.begin(), v.end(), std::uint64_t{mid}); };
        std::vector<std::uint64_t> want(ours.begin(), split(ours) + 1);
        want.insert(want.end(), split(theirs) + 1, theirs.end());
        CAPTURE(round);
        CHECK(merged == want);
    }
}
