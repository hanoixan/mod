#include "syntax/merge_colors.hpp"

#include <algorithm>
#include <span>
#include <cstddef>
#include <utility>

#include "edit/merge3.hpp"
#include "text/utf8.hpp"

namespace mod {
namespace {

using Range = std::pair<std::size_t, std::size_t>;  // [first, second) bytes of the text

// A section's characters: each one's value (a code point, or a byte that is not UTF-8 moved
// past the code points) and its first byte, with the section's end last.
struct Chars {
    std::vector<std::uint64_t> value;
    std::vector<std::size_t> at;
};

Chars chars_of(std::string_view text, Range r) {
    Chars c;
    std::size_t i = r.first;
    while (i < r.second) {
        const Decoded d = decode(std::as_bytes(std::span(text.data() + i, r.second - i)));
        const std::size_t len = d.valid ? std::max<std::size_t>(1, d.len) : 1;
        c.value.push_back(d.valid ? static_cast<std::uint64_t>(d.cp) : 0x110000u + static_cast<unsigned char>(text[i]));
        c.at.push_back(i);
        i += len;
    }
    c.at.push_back(r.second);
    return c;
}

// The changes from `a` to `b`, as character indexes: the common start and end are left out
// before comparing, and a middle too large to compare, or a comparison that fails, is one
// change.
std::vector<LineEdit> char_edits(const Chars& a, const Chars& b) {
    const std::size_t na = a.value.size();
    const std::size_t nb = b.value.size();
    std::size_t pre = 0;
    while (pre < na && pre < nb && a.value[pre] == b.value[pre]) ++pre;
    std::size_t suf = 0;
    while (suf < na - pre && suf < nb - pre && a.value[na - 1 - suf] == b.value[nb - 1 - suf]) ++suf;
    const std::size_t a0 = pre, a1 = na - suf, b0 = pre, b1 = nb - suf;
    if (a0 == a1 && b0 == b1) return {};
    const LineEdit whole{a0, a1, b0, b1};
    if (a0 == a1 || b0 == b1 || a.at[a1] - a.at[a0] > kMergeDiffMaxBytes || b.at[b1] - b.at[b0] > kMergeDiffMaxBytes) return {whole};
    const auto edits = diff_lines(std::span(a.value).subspan(a0, a1 - a0), std::span(b.value).subspan(b0, b1 - b0));
    if (!edits) return {whole};
    std::vector<LineEdit> out;
    out.reserve(edits->size());
    for (const LineEdit& e : *edits) out.push_back(LineEdit{a0 + e.a0, a0 + e.a1, b0 + e.b0, b0 + e.b1});
    return out;
}

// Marks a's and b's characters that `edits` changes.
void mark_changed(const std::vector<LineEdit>& edits, std::vector<bool>& a, std::vector<bool>& b) {
    for (const LineEdit& e : edits) {
        std::fill(a.begin() + static_cast<std::ptrdiff_t>(e.a0), a.begin() + static_cast<std::ptrdiff_t>(e.a1), true);
        std::fill(b.begin() + static_cast<std::ptrdiff_t>(e.b0), b.begin() + static_cast<std::ptrdiff_t>(e.b1), true);
    }
}

// Calls `f(i, j)` for each a[i] that `edits` keeps as b[j], in order.
template <typename F>
void each_kept(const std::vector<LineEdit>& edits, std::size_t na, std::size_t nb, F f) {
    std::size_t i = 0, j = 0;
    for (const LineEdit& e : edits) {
        for (; i < e.a0 && j < e.b0; ++i, ++j) f(i, j);
        i = e.a1;
        j = e.b1;
    }
    for (; i < na && j < nb; ++i, ++j) f(i, j);
}

// Which marker the line [from, to) (its line feed left out) is: one of "<|=>", or 0.
char marker_of(std::string_view text, std::size_t from, std::size_t to) {
    if (to - from < 7) return 0;
    const char c = text[from];
    if (c != '<' && c != '|' && c != '=' && c != '>') return 0;
    for (std::size_t i = 1; i < 7; ++i)
        if (text[from + i] != c) return 0;
    if (to - from > 7 && text[from + 7] != ' ' && text[from + 7] != '\r') return 0;
    return c;
}

}  // namespace

std::vector<StyleSpan> merge_color_spans(std::string_view text, std::uint64_t base) {
    std::vector<StyleSpan> out;
    // The block being read: its marker lines so far ([start, end) without the line feed) and
    // where each line after one begins.
    Range banner[4];
    std::size_t after[4] = {};
    int seen = 0;  // markers of the block so far, in order

    // A section's characters in their styles (none where `style` is Default), runs joined.
    auto emit = [&](const Chars& c, const std::vector<Style>& style) {
        for (std::size_t i = 0; i < style.size(); ++i) {
            if (style[i] == Style::Default) continue;
            const std::uint64_t from = base + c.at[i];
            const std::uint64_t to = base + c.at[i + 1];
            if (!out.empty() && out.back().style == style[i] && out.back().end == from) out.back().end = to;
            else out.push_back(StyleSpan{from, to, style[i], 0});
        }
    };
    auto finish_block = [&](Range last, std::size_t last_start) {
        const Chars y = chars_of(text, {after[0], banner[1].first});
        const Chars b = chars_of(text, {after[1], banner[2].first});
        const Chars t = chars_of(text, {after[2], last_start});
        const std::size_t ny = y.value.size(), nb = b.value.size(), nt = t.value.size();
        // Each side against the base: what it inserted, and what of the base it deleted.
        std::vector<bool> ins_y(ny), ins_t(nt), del_y(nb), del_t(nb);
        mark_changed(char_edits(y, b), ins_y, del_y);
        mark_changed(char_edits(t, b), ins_t, del_t);
        // Yours against theirs: an insertion both made is one the two sides keep in common.
        std::vector<bool> both_y(ny), both_t(nt);
        each_kept(char_edits(y, t), ny, nt, [&](std::size_t i, std::size_t j) {
            if (ins_y[i] && ins_t[j]) both_y[i] = both_t[j] = true;
        });
        std::vector<Style> sy(ny), sb(nb), st(nt);
        for (std::size_t i = 0; i < ny; ++i)
            if (ins_y[i]) sy[i] = both_y[i] ? Style::merge_both_insertion : Style::merge_your_insertion;
        for (std::size_t i = 0; i < nt; ++i)
            if (ins_t[i]) st[i] = both_t[i] ? Style::merge_both_insertion : Style::merge_their_insertion;
        for (std::size_t i = 0; i < nb; ++i)
            if (del_y[i] || del_t[i])
                sb[i] = del_y[i] && del_t[i] ? Style::merge_both_deletion : del_y[i] ? Style::merge_your_deletion : Style::merge_their_deletion;
        auto ban = [&](Range r) { out.push_back(StyleSpan{base + r.first, base + r.second, Style::merge_banner, 0}); };
        ban(banner[0]);
        emit(y, sy);
        ban(banner[1]);
        emit(b, sb);
        ban(banner[2]);
        emit(t, st);
        ban(last);
    };

    static constexpr char kOrder[] = "<|=>";
    std::size_t line = 0;
    while (line < text.size()) {
        const std::size_t nl = text.find('\n', line);
        const std::size_t end = nl == std::string_view::npos ? text.size() : nl;
        const std::size_t next = nl == std::string_view::npos ? text.size() : nl + 1;
        std::size_t shown_end = end;
        if (shown_end > line && text[shown_end - 1] == '\r') --shown_end;
        const char m = marker_of(text, line, end);
        if (m == '<') {
            seen = 1;  // a block starts here, even inside another
            banner[0] = {line, shown_end};
            after[0] = next;
        } else if (m != 0 && seen > 0) {
            if (m != kOrder[seen]) {
                seen = 0;  // out of order: not a diff3 block
            } else if (m == '>') {
                finish_block({line, shown_end}, line);
                seen = 0;
            } else {
                banner[seen] = {line, shown_end};
                after[seen] = next;
                ++seen;
            }
        }
        line = next;
    }
    return out;
}

}  // namespace mod
