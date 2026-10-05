#pragma once

#include <memory>

#include "syntax/highlight.hpp"

namespace mod {

// Two highlighters as one: the overlay's spans are drawn over the base's, which are cut
// around them. Every document event goes to the base, then the overlay. Main thread.
class LayeredHighlighter final : public Highlighter {
public:
    LayeredHighlighter(std::unique_ptr<Highlighter> base, std::unique_ptr<Highlighter> overlay)
        : base_(std::move(base)), overlay_(std::move(overlay)) {}

    std::vector<StyleSpan> spans_for_line(std::uint64_t line_start, std::string_view line_bytes) override;
    void visible_range_changed(std::uint64_t first_offset, std::uint64_t last_offset) override;
    std::optional<Clock::time_point> tick(Clock::time_point now) override;
    std::string status() const override { return overlay_->status(); }

    void before_change(const ChangeEvent& ev) override;
    void after_change(const ChangeEvent& ev) override;
    void reloaded() override;
    void saved() override;

private:
    std::unique_ptr<Highlighter> base_;
    std::unique_ptr<Highlighter> overlay_;
};

}  // namespace mod
