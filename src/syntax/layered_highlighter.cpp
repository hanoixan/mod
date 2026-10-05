#include "syntax/layered_highlighter.hpp"

#include <algorithm>
#include <iterator>

namespace mod {

std::vector<StyleSpan> LayeredHighlighter::spans_for_line(std::uint64_t line_start, std::string_view line_bytes) {
    std::vector<StyleSpan> base = base_->spans_for_line(line_start, line_bytes);
    std::vector<StyleSpan> over = overlay_->spans_for_line(line_start, line_bytes);
    if (over.empty()) return base;
    if (base.empty()) return over;

    // The base's spans with the overlay's ranges cut out of them.
    std::vector<StyleSpan> cut;
    std::size_t j = 0;
    for (const StyleSpan& b : base) {
        while (j < over.size() && over[j].end <= b.start) ++j;
        std::uint64_t cur = b.start;
        for (std::size_t k = j; k < over.size() && over[k].start < b.end; ++k) {
            if (over[k].start > cur) cut.push_back(StyleSpan{cur, over[k].start, b.style, b.modifiers});
            cur = std::max(cur, over[k].end);
        }
        if (cur < b.end) cut.push_back(StyleSpan{cur, b.end, b.style, b.modifiers});
    }
    std::vector<StyleSpan> out;
    out.reserve(cut.size() + over.size());
    std::ranges::merge(cut, over, std::back_inserter(out), {}, &StyleSpan::start, &StyleSpan::start);
    return out;
}

void LayeredHighlighter::visible_range_changed(std::uint64_t first_offset, std::uint64_t last_offset) {
    base_->visible_range_changed(first_offset, last_offset);
    overlay_->visible_range_changed(first_offset, last_offset);
}

std::optional<Highlighter::Clock::time_point> LayeredHighlighter::tick(Clock::time_point now) {
    const auto a = base_->tick(now);
    const auto b = overlay_->tick(now);
    if (!a) return b;
    if (!b) return a;
    return std::min(*a, *b);
}

void LayeredHighlighter::before_change(const ChangeEvent& ev) {
    base_->before_change(ev);
    overlay_->before_change(ev);
}

void LayeredHighlighter::after_change(const ChangeEvent& ev) {
    base_->after_change(ev);
    overlay_->after_change(ev);
}

void LayeredHighlighter::reloaded() {
    base_->reloaded();
    overlay_->reloaded();
}

void LayeredHighlighter::saved() {
    base_->saved();
    overlay_->saved();
}

}  // namespace mod
