#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "syntax/highlight.hpp"

namespace mod {

// The largest section, in bytes, whose characters are compared with base. A larger one has
// its differing middle (what lies between the common start and end) colored whole, so the
// comparison never holds up a keystroke.
inline constexpr std::size_t kMergeDiffMaxBytes = 32 * 1024;

// The colors of the diff3 conflict blocks in `text`, whose first byte is at offset `base`.
// A block is four marker lines in order, each 7 of its character at a line's start followed
// by a space, a CR or the line's end: `<<<<<<<` (yours), `|||||||` (the common base),
// `=======` (theirs), `>>>>>>>`, as git and mod's own merge write them; the sides are known by
// their place, and labels after the markers are free. A marker out of
// order ends the block being read without coloring it (a `<<<<<<<` starts a new one), so a
// block that is being edited and is no longer whole is plain text again.
//
// Each marker line, its line feed left out, is Style::merge_banner. The characters yours has
// that the base has not are merge_your_insertion, theirs' merge_their_insertion; the base's
// characters that yours deleted or replaced are merge_your_deletion, theirs'
// merge_their_deletion, and both's merge_both_deletion. An insertion both sides made (the same
// characters, kept as each other's when yours is compared with theirs) is merge_both_insertion
// in each. Everything else has no span. Characters
// are code points (a byte that is not UTF-8 counts as one). The spans are sorted and do not
// overlap. Pure: no state, any thread.
std::vector<StyleSpan> merge_color_spans(std::string_view text, std::uint64_t base);

}  // namespace mod
