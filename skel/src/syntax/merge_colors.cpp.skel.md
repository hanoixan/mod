---
role: product
unit: ./merge_colors.hpp.skel.md
stamp: source 92d21fad, stand-in 89787b83
---
# module: merge_colors (implementation)

Implements [merge_color_spans](./merge_colors.hpp.skel.md#function-merge_color_spans) in one pass over the lines, remembering the block being read: each marker line's range and where the line after it starts. Sections are compared as characters decoded with [decode](../text/utf8.hpp.skel.md#function-decode) (a byte that is not UTF-8 is given a value past the code points, so it equals only itself). At `>>>>>>>` each side is compared with the base, marking the side's insertions and, per base character, which sides deleted or replaced it; then yours is compared with theirs, and a character inserted in both that the comparison keeps as the other's is marked as inserted by both, in each section. A comparison strips the common start and end first, and a middle over `kMergeDiffMaxBytes`, or a diff that fails, is one whole change. Styles are given per character and runs of one style joined into spans, emitted in text order with the banners.

- **Owns:** nothing beyond the header's.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [merge_color_spans](./merge_colors.hpp.skel.md#function-merge_color_spans)
- **Depends on:** [diff_lines](../edit/merge3.hpp.skel.md#function-diff_lines)
- **Depends on:** [decode](../text/utf8.hpp.skel.md#function-decode)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
