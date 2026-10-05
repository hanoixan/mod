#include "ui/reading_layout.hpp"

#include <algorithm>
#include <span>

#include "text/utf8.hpp"

namespace mod {

ReadingLayout::ReadingLayout(std::string_view text, int width) : page_(render_markdown(text, width)) {
    for (std::size_t l = 0; l < page_.lines.size(); ++l) {
        const RenderedLine& line = page_.lines[l];
        int col = 0;
        std::optional<Entry> last;  // the line's last shown code point, as its end
        for (std::size_t b = 0; b < line.text.size();) {
            const Decoded d = decode(std::as_bytes(std::span(line.text.data() + b, line.text.size() - b)));
            const std::size_t len = std::max<std::size_t>(1, d.len);
            const int w = d.valid ? std::max(0, display_width(d, 0, 1)) : 1;
            if (b < line.source.size() && line.source[b] != kGenerated) {
                entries_.push_back(Entry{line.source[b], Spot{l, b, col}, false});
                last = Entry{line.source[b] + len, Spot{l, b + len, col + w}, true};
            }
            col += w;
            b += len;
        }
        if (last) entries_.push_back(*last);
    }
    // In source order; where a line's end has the offset of a shown byte, the byte wins.
    std::ranges::stable_sort(entries_, [](const Entry& a, const Entry& b) { return a.offset != b.offset ? a.offset < b.offset : !a.end && b.end; });
    entries_.erase(std::ranges::unique(entries_, [](const Entry& a, const Entry& b) { return a.offset == b.offset; }).begin(), entries_.end());
    by_line_.resize(page_.lines.size());
    for (std::size_t i = 0; i < entries_.size(); ++i) by_line_[entries_[i].spot.line].push_back(i);
    for (auto& v : by_line_) std::ranges::sort(v, [&](std::size_t a, std::size_t b) { return entries_[a].spot.col < entries_[b].spot.col; });
}

std::size_t ReadingLayout::index_at(std::uint64_t offset) const {
    const auto it = std::ranges::lower_bound(entries_, offset, {}, &Entry::offset);
    return it == entries_.end() ? entries_.size() - 1 : static_cast<std::size_t>(it - entries_.begin());
}

ReadingLayout::Spot ReadingLayout::locate(std::uint64_t offset) const {
    if (entries_.empty()) return {};
    return entries_[index_at(offset)].spot;
}

std::optional<std::size_t> ReadingLayout::line_with_positions(std::size_t target, int dir) const {
    for (int pass = 0; pass < 2; ++pass, dir = -dir) {
        for (auto l = static_cast<std::ptrdiff_t>(target); l >= 0 && l < static_cast<std::ptrdiff_t>(by_line_.size()); l += dir) {
            if (!by_line_[static_cast<std::size_t>(l)].empty()) return static_cast<std::size_t>(l);
        }
    }
    return std::nullopt;
}

// The position on `line` nearest at or before `col`, else its first.
std::uint64_t ReadingLayout::on_line(std::size_t line, int col) const {
    const auto& v = by_line_[line];
    std::size_t pick = v.front();
    for (std::size_t i : v) {
        if (entries_[i].spot.col > col) break;
        pick = i;
    }
    return entries_[pick].offset;
}

std::uint64_t ReadingLayout::move(Motion motion, std::uint64_t cursor, std::size_t page_lines, std::optional<int>& sticky) const {
    if (entries_.empty()) return cursor;
    const std::size_t at = index_at(cursor);
    const Spot spot = entries_[at].spot;
    const bool vertical = motion == Motion::Up || motion == Motion::Down || motion == Motion::PageUp || motion == Motion::PageDown;
    if (!vertical) sticky.reset();
    switch (motion) {
        case Motion::Right:
        case Motion::WordRight: {
            const auto it = std::ranges::upper_bound(entries_, cursor, {}, &Entry::offset);
            return it == entries_.end() ? entries_.back().offset : it->offset;
        }
        case Motion::Left:
        case Motion::WordLeft: {
            const auto it = std::ranges::lower_bound(entries_, cursor, {}, &Entry::offset);
            return it == entries_.begin() ? entries_.front().offset : std::prev(it)->offset;
        }
        case Motion::LineStart: return entries_[by_line_[spot.line].front()].offset;
        case Motion::LineEnd: return entries_[by_line_[spot.line].back()].offset;
        case Motion::DocStart: return entries_.front().offset;
        case Motion::DocEnd: return entries_.back().offset;
        case Motion::Up:
        case Motion::Down:
        case Motion::PageUp:
        case Motion::PageDown: {
            const int col = sticky.value_or(spot.col);
            sticky = col;
            const int dir = motion == Motion::Up || motion == Motion::PageUp ? -1 : 1;
            const bool page = motion == Motion::PageUp || motion == Motion::PageDown;
            std::optional<std::size_t> line;
            if (page) {
                const auto step = static_cast<std::ptrdiff_t>(std::max<std::size_t>(1, page_lines));
                const std::ptrdiff_t want = std::clamp<std::ptrdiff_t>(static_cast<std::ptrdiff_t>(spot.line) + dir * step, 0,
                                                                       static_cast<std::ptrdiff_t>(by_line_.size()) - 1);
                line = line_with_positions(static_cast<std::size_t>(want), dir);
            } else {
                // The next line in that direction with positions; none, and the cursor stays.
                for (auto l = static_cast<std::ptrdiff_t>(spot.line) + dir; l >= 0 && l < static_cast<std::ptrdiff_t>(by_line_.size()); l += dir) {
                    if (!by_line_[static_cast<std::size_t>(l)].empty()) {
                        line = static_cast<std::size_t>(l);
                        break;
                    }
                }
                if (!line) return cursor;
            }
            return line ? on_line(*line, col) : cursor;
        }
    }
    return cursor;
}

std::string ReadingLayout::visible_text(std::uint64_t start, std::uint64_t end) const {
    std::string out;
    std::optional<std::uint32_t> last_block;  // the source line of the last line that gave text
    for (const RenderedLine& line : page_.lines) {
        std::string got;
        std::string pending;  // made-up bytes, kept only when shown bytes follow them
        bool included = false;
        bool first_real = true;
        for (std::size_t b = 0; b < line.text.size(); ++b) {
            const std::uint64_t src = b < line.source.size() ? line.source[b] : kGenerated;
            if (src == kGenerated) {
                pending += line.text[b];
                continue;
            }
            const bool in = src >= start && src < end;
            if (in) {
                if (included || first_real) got += pending;
                got += line.text[b];
                included = true;
            } else if (included) {
                break;
            }
            pending.clear();
            first_real = false;
        }
        if (!included) continue;
        if (last_block) out += *last_block == line.source_line ? " " : "\n";
        out += got;
        last_block = line.source_line;
    }
    return out;
}

}  // namespace mod
