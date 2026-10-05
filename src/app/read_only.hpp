#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "syntax/markdown.hpp"

namespace mod {

// What following a link asks the caller to do.
struct LinkAction {
    enum Kind { none, jump, open, message } kind = none;
    std::uint64_t offset = 0;      // jump: where to put the cursor in the shown file
    std::filesystem::path path;    // open: the file to show
    std::string anchor;            // open: a heading slug to jump to once it is shown, or empty
    std::string text;              // message: for the status line
};

// A place in the trail of a read-only view: a file and where the view was in it.
struct TrailEntry {
    std::filesystem::path path;
    std::uint64_t cursor = 0;
    std::uint64_t top = 0;
};

// Link navigation and the back/forward trail of one read-only view. Knows nothing of
// documents or the screen: the caller shows files and moves the cursor. Main thread.
class ReadOnlyNav {
public:
    // The link after (`direction` 1) or before (-1) `pos`, wrapping; nullopt without links.
    static std::optional<MarkdownLink> next_link(const MarkdownOutline& outline, std::uint64_t pos, int direction);
    // The link whose source covers `pos`.
    static std::optional<MarkdownLink> link_at(const MarkdownOutline& outline, std::uint64_t pos);
    // What following `link` means, from the file `shown` with outline `here`.
    static LinkAction resolve(const MarkdownLink& link, const std::filesystem::path& shown, const MarkdownOutline& here);
    // The start of the heading whose slug is `slug`.
    static std::optional<std::uint64_t> find_anchor(const MarkdownOutline& outline, std::string_view slug);

    // Starts a trail at the base file.
    void reset(TrailEntry base);
    // Leaves `current` (the position in the entry being shown) for `next`; forward entries go.
    void visit(TrailEntry current, TrailEntry next);
    // The entry to show after going back or forward, with `current` saved; nullopt at an end.
    std::optional<TrailEntry> back(TrailEntry current);
    std::optional<TrailEntry> forward(TrailEntry current);

    const TrailEntry& here() const { return trail_[index_]; }
    std::size_t index() const noexcept { return index_; }
    std::size_t size() const noexcept { return trail_.size(); }
    // Whether the shown entry is the base file (index 0).
    bool at_base() const noexcept { return index_ == 0; }

private:
    std::vector<TrailEntry> trail_{TrailEntry{}};
    std::size_t index_ = 0;
};

}  // namespace mod
