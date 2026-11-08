#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <stop_token>
#include <vector>

namespace mod {

// The lines of a text as hashes, for diffing without holding the text: line i is the bytes
// [start[i], start[i + 1]), its line feed included (the last line may have none). Built by
// feeding the text in pieces of any size.
struct LineIndex {
    std::vector<std::uint64_t> hash;
    std::vector<std::uint64_t> start;  // one more than `hash`: the text's end last

    std::size_t lines() const noexcept { return hash.size(); }
};

class LineIndexer {
public:
    void feed(std::span<const std::byte> bytes);
    LineIndex finish();

private:
    LineIndex index_{{}, {0}};
    std::uint64_t at_ = 0;    // bytes fed
    std::uint64_t h_ = kSeed;  // the open line's hash so far
    bool open_ = false;       // a line has begun and not ended
    static constexpr std::uint64_t kSeed = 0xcbf29ce484222325ull;  // FNV-1a
};

// One change from `a` to `b`: a's lines [a0, a1) become b's lines [b0, b1).
struct LineEdit {
    std::size_t a0 = 0, a1 = 0, b0 = 0, b1 = 0;
    friend bool operator==(const LineEdit&, const LineEdit&) = default;
};

// The changes from `a` to `b` (Myers' O(ND) diff, in linear space), in order. Lines are equal
// when their hashes are. `progress`, when given, counts a's lines settled so far; `stop` ends it
// early with nullopt.
std::optional<std::vector<LineEdit>> diff_lines(std::span<const std::uint64_t> a, std::span<const std::uint64_t> b, std::stop_token stop = {},
                                                std::atomic<std::size_t>* progress = nullptr);

// A place where the three-way merge changes "ours" (the text being edited): its lines
// [ours0, ours1) become theirs [theirs0, theirs1) (`take_theirs`, a change only the other side
// made), or a conflict block showing theirs, the base's [base0, base1) and ours.
struct MergeHunk {
    enum Kind { take_theirs, conflict } kind = take_theirs;
    std::size_t base0 = 0, base1 = 0, ours0 = 0, ours1 = 0, theirs0 = 0, theirs1 = 0;
    friend bool operator==(const MergeHunk&, const MergeHunk&) = default;
};

// diff3: what to change in `ours` to merge in what `theirs` changed from `base`, in order of
// `ours`. Changes of one side alone are taken as they are (ours' already are); the same change
// on both sides is taken once; changes of both sides that overlap, or touch at one place, are a
// conflict. `progress` counts base lines settled across both diffs (2 × base's lines in all).
std::optional<std::vector<MergeHunk>> merge3(std::span<const std::uint64_t> base, std::span<const std::uint64_t> ours,
                                             std::span<const std::uint64_t> theirs, const std::stop_token& stop = {},
                                             std::atomic<std::size_t>* progress = nullptr);

}  // namespace mod
