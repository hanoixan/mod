#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "text/piece_tree.hpp"
#include "text/wrap.hpp"

using namespace mod;

namespace {

constexpr std::uint64_t npos = WrapLayout::npos;

struct Text {
    PieceTree tree;
    explicit Text(std::string_view s) { tree.insert(0, std::as_bytes(std::span(s.data(), s.size()))); }
};

// Every row of the text, in order, as (start, end) pairs, walking with next_row.
std::vector<std::pair<std::uint64_t, std::uint64_t>> rows(const WrapLayout& w) {
    std::vector<std::pair<std::uint64_t, std::uint64_t>> out;
    for (std::uint64_t r = 0; r != npos; r = w.next_row(r)) {
        out.emplace_back(r, w.row_end(r));
        REQUIRE(out.size() < 100'000);
    }
    return out;
}

std::vector<std::string> row_texts(const Text& t, const WrapLayout& w) {
    std::vector<std::string> out;
    for (const auto& [start, end] : rows(w)) out.push_back(t.tree.read(start, end - start));
    return out;
}

using Rows = std::vector<std::string>;

}  // namespace

TEST_CASE("wrap_row_length: a row breaks after the last whitespace that fits") {
    CHECK(wrap_row_length("hello world", 7, 4) == 6);    // "hello " | "world"
    CHECK(wrap_row_length("hello world", 11, 4) == 11);  // fits exactly
    CHECK(wrap_row_length("hello world", 80, 4) == 11);
    CHECK(wrap_row_length("one two three", 9, 4) == 8);  // "one two " | "three"
    CHECK(wrap_row_length("", 10, 4) == 0);
}

TEST_CASE("wrap_row_length: a word longer than the row breaks at the edge") {
    CHECK(wrap_row_length("abcdefghij", 4, 4) == 4);
    CHECK(wrap_row_length("abcd", 4, 4) == 4);
    CHECK(wrap_row_length("ab abcdefghij", 4, 4) == 3);  // the short word first, then the long one alone
}

TEST_CASE("wrap_row_length: whitespace that does not fit stays at the end of the row") {
    CHECK(wrap_row_length("hello world", 5, 4) == 6);  // "hello " | "world", not "hello" | " world"
    CHECK(wrap_row_length("ab   cd", 2, 4) == 5);      // "ab   " | "cd"
    CHECK(wrap_row_length("ab   ", 2, 4) == 5);
}

TEST_CASE("wrap_row_length: wide characters, combining marks and tabs") {
    const std::string cjk = "\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E";  // three wide characters
    CHECK(wrap_row_length(cjk, 5, 4) == 6);  // two fit; the third would need column 6
    CHECK(wrap_row_length(cjk, 4, 4) == 6);
    CHECK(wrap_row_length(cjk, 1, 4) == 3);  // a row always takes at least one cluster
    const std::string accent = "ae\xCC\x81";  // "a", then "e" + combining acute as one cluster
    CHECK(wrap_row_length(accent, 1, 4) == 1);
    CHECK(wrap_row_length(accent.substr(1), 1, 4) == 3);
    CHECK(wrap_row_length("a\tb", 4, 4) == 2);   // the tab runs to column 4, "b" does not fit
    CHECK(wrap_row_length("a\tb", 5, 4) == 3);
    CHECK(wrap_row_length("a\tb", 8, 8) == 2);   // tab stops follow the tab width
    CHECK(wrap_row_length("\x01\x01\x01", 4, 4) == 2);  // a control character shows as two cells
}

TEST_CASE("rows of a short text: words, an empty line, a final line feed") {
    const Text t("aaaa bbbb cccc dddd\n\nxx\n");
    const WrapLayout w(t.tree, 10, 4);
    CHECK(row_texts(t, w) == Rows{"aaaa bbbb ", "cccc dddd", "", "xx", ""});
    CHECK(w.row_start(0) == 0);
    CHECK(w.row_start(9) == 0);
    CHECK(w.row_start(10) == 10);  // where one row ends and the next begins: the next
    CHECK(w.row_start(19) == 10);  // the end of the line: the line's last row
    CHECK(w.row_start(20) == 20);  // the empty line
    CHECK(w.row_start(22) == 21);
    CHECK(w.row_end(0) == 10);
    CHECK(w.row_end(10) == 19);
    CHECK(w.row_end(20) == 20);
    CHECK(w.next_row(0) == 10);
    CHECK(w.next_row(10) == 20);
    CHECK(w.next_row(20) == 21);
    CHECK(w.next_row(21) == 24);  // the empty row after the final line feed
    CHECK(w.next_row(24) == npos);
    CHECK(w.prev_row(24) == 21);
    CHECK(w.prev_row(21) == 20);
    CHECK(w.prev_row(20) == 10);
    CHECK(w.prev_row(10) == 0);
    CHECK(w.prev_row(0) == npos);
    CHECK(w.line_start(15) == 0);
    CHECK(w.line_end(0) == 19);
}

TEST_CASE("an empty text is one empty row; CR LF ends a line before the CR") {
    const Text empty("");
    const WrapLayout we(empty.tree, 10, 4);
    CHECK(rows(we) == std::vector<std::pair<std::uint64_t, std::uint64_t>>{{0, 0}});
    CHECK(we.row_start(0) == 0);
    CHECK(we.prev_row(0) == npos);

    const Text t("abcdef\r\ngh");
    const WrapLayout w(t.tree, 4, 4);
    CHECK(row_texts(t, w) == Rows{"abcd", "ef", "gh"});
    CHECK(w.line_end(0) == 6);
    CHECK(w.row_start(6) == 4);
    CHECK(w.prev_row(8) == 4);
}

TEST_CASE("a width of one column still makes progress, also with wide characters") {
    const Text t("ab\xE6\x97\xA5");
    const WrapLayout w(t.tree, 1, 4);
    CHECK(row_texts(t, w) == Rows{"a", "b", "\xE6\x97\xA5"});
    const WrapLayout zero(t.tree, 0, 4);  // treated as one column
    CHECK(row_texts(t, zero).size() == 3);
}

TEST_CASE("a line longer than a block is laid out block by block") {
    const Text t(std::string(10'000, 'x') + "\ny");
    const WrapLayout w(t.tree, 80, 4);
    // 4096 = 51 * 80 + 16: the row that starts at 4080 is cut short by the block's end.
    CHECK(w.row_end(4000) == 4080);
    CHECK(w.row_end(4080) == kWrapBlock);
    CHECK(w.row_end(kWrapBlock) == kWrapBlock + 80);
    CHECK(w.row_start(4090) == 4080);
    CHECK(w.row_start(kWrapBlock) == kWrapBlock);
    CHECK(w.row_start(kWrapBlock + 5) == kWrapBlock);
    CHECK(w.prev_row(kWrapBlock) == 4080);
    CHECK(w.next_row(4080) == kWrapBlock);
    CHECK(w.row_start(10'000) == 2 * kWrapBlock + 22 * 80);  // the line end: its last row
    CHECK(w.row_end(2 * kWrapBlock + 22 * 80) == 10'000);
    CHECK(w.next_row(2 * kWrapBlock + 22 * 80) == 10'001);
    // Walking forward and backward visits the same rows, and they tile the text.
    const auto all = rows(w);
    std::uint64_t expect = 0;
    for (std::size_t i = 0; i + 1 < all.size(); ++i) {
        CHECK(all[i].first == expect);
        expect = all[i].second == 10'000 ? 10'001 : all[i].second;
    }
    std::uint64_t r = all.back().first;
    for (std::size_t i = all.size(); i-- > 1;) {
        r = w.prev_row(r);
        REQUIRE(r == all[i - 1].first);
    }
}

TEST_CASE("a block boundary never splits a multi-byte character") {
    std::string s = "a";
    for (int i = 0; i < 3000; ++i) s += "\xC3\xA9";  // two bytes each, at odd offsets
    const Text t(s);
    const WrapLayout w(t.tree, 80, 4);
    for (const auto& [start, end] : rows(w)) {
        CAPTURE(start);
        if (start < s.size()) CHECK((static_cast<unsigned char>(s[start]) & 0xC0) != 0x80);
        CHECK(end > start);
    }
    CHECK(w.row_start(kWrapBlock + 1) == kWrapBlock + 1);  // byte 4096 continues the character at 4095
    CHECK(w.row_start(kWrapBlock) < kWrapBlock);
}
