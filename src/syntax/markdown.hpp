#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "syntax/highlight.hpp"

namespace mod {

// A link in a Markdown document, with absolute byte offsets: the whole link as written,
// the text that shows (for an autolink, the address), and its destination with any
// reference resolved and backslash escapes removed.
struct MarkdownLink {
    std::uint64_t start = 0;
    std::uint64_t end = 0;
    std::uint64_t text_start = 0;
    std::uint64_t text_end = 0;
    std::string target;
};

// A heading line and its GitHub-style anchor slug.
struct MarkdownHeading {
    std::uint64_t start = 0;
    std::string slug;
};

// The links and headings of a document, in document order.
struct MarkdownOutline {
    std::vector<MarkdownLink> links;
    std::vector<MarkdownHeading> headings;
    bool truncated = false;  // the document is longer than the scan cap, or has more links
};

inline constexpr std::uint64_t kMaxOutlineBytes = std::uint64_t{16} << 20;
// At most this many links are collected; a document with more is marked truncated.
inline constexpr std::size_t kMaxOutlineLinks = 100'000;

// Scans whole lines up to `max_bytes`, skipping fenced code blocks.
MarkdownOutline scan_markdown(const PieceTree& text, std::uint64_t max_bytes = kMaxOutlineBytes);
// GitHub's anchor for a heading: lower case, spaces to '-', other punctuation removed.
std::string heading_slug(std::string_view heading_text);

// The fence state at a line start.
struct MarkdownBlockState {
    bool in_fence = false;
    char fence_char = 0;
    std::uint32_t fence_len = 0;

    friend bool operator==(const MarkdownBlockState&, const MarkdownBlockState&) = default;
};

// A recorded state. `trusted` is false when it derives from an assumed "no open fence"
// past the back-scan bound.
struct MarkdownCheckpoint {
    MarkdownBlockState state;
    bool trusted = true;
};

// In-place Markdown styling: markup stays visible (dimmed), content gets its style.
class MarkdownHighlighter final : public Highlighter {
public:
    // Fence state is assumed closed past this many bytes before the nearest checkpoint.
    static constexpr std::uint64_t kMaxBackScan = std::uint64_t{4} << 20;
    static constexpr std::uint64_t kCheckpointLines = 256;
    static constexpr std::size_t kMaxDelimiters = 1024;

    using BlockState = MarkdownBlockState;
    using Checkpoint = MarkdownCheckpoint;

    explicit MarkdownHighlighter(const Document& doc) : doc_(doc) {}

    std::vector<StyleSpan> spans_for_line(std::uint64_t line_start, std::string_view line_bytes) override;

    void after_change(const ChangeEvent& ev) override;
    void reloaded() override;

    // For tests: the recorded checkpoints, by line-start offset.
    const std::map<std::uint64_t, Checkpoint>& checkpoints() const noexcept { return checkpoints_; }

private:
    struct LineState {
        BlockState state;
        bool trusted = true;
        std::uint64_t lines_since_checkpoint = 0;
    };
    LineState state_at(std::uint64_t line_start);

    const Document& doc_;
    std::map<std::uint64_t, Checkpoint> checkpoints_;
    // The state at the start of the line after the last one highlighted, so that
    // consecutive lines cost O(1).
    std::optional<std::pair<std::uint64_t, LineState>> next_line_;
};

}  // namespace mod
