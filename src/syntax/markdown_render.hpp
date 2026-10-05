#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "syntax/highlight.hpp"

namespace mod {

// A styled run of a rendered line, in bytes of its text.
struct RenderSpan {
    std::size_t begin = 0;
    std::size_t end = 0;
    Style style = Style::Default;
};

// A source-map entry for a byte the layout made up (a bullet, a rule, padding).
inline constexpr std::uint64_t kGenerated = UINT64_MAX;

struct RenderedLine {
    std::string text;
    std::vector<RenderSpan> spans;  // sorted, not overlapping; unstyled text has none
    std::uint32_t source_line = 0;  // 1-based line of the source it came from
    std::vector<std::uint64_t> source;  // per byte of `text`: its offset in the source, or kGenerated
};

// Where a link's text lies on one rendered line.
struct LinkPiece {
    std::size_t line = 0;
    std::size_t begin = 0;
    std::size_t end = 0;
};

struct RenderedLink {
    std::string target;
    std::vector<LinkPiece> pieces;  // one per line the text wraps onto
};

struct RenderedPage {
    std::vector<RenderedLine> lines;
    std::vector<RenderedLink> links;                            // in reading order
    std::vector<std::pair<std::string, std::size_t>> anchors;  // heading slug → its rendered line
};

// Lays a Markdown page out for reading at `width` columns (at least 20): what the
// Markdown means, without its marks.
RenderedPage render_markdown(std::string_view text, int width);

}  // namespace mod
