---
role: test
stamp: source a3519604, stand-in 6f9c0982
---
# module: merge_colors_test

[merge_color_spans](../src/syntax/merge_colors.hpp.skel.md#function-merge_color_spans) on fixed texts, each span checked as its style and the text it covers: a block's four banners (labels included, line feed left out) and the characters each side changed against the base, yours' and theirs' in their own styles; offsets shifted by `base`; git's labels, CRLF lines (the CR not in a banner), whole lines added and removed, and a deletion by both; identical sides with only their banners; an empty section; blocks that are not whole or are out of order (no end, two-way with no base, no start, a marker out of order, eight characters, no space after, not at a line's start) with no spans; a stray `<<<<<<<` restarting the block, two blocks, and plain text between them; characters as code points (`é` one character) and the base's deletions told apart by side (yours, theirs, both); a section past [kMergeDiffMaxBytes](../src/syntax/merge_colors.hpp.skel.md#symbol-kmergediffmaxbytes) with its differing middle colored whole; an insertion both sides made colored as both's in each side; the same insertion in different places each side's own.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; the function is pure.
- **Depends on:** [merge_color_spans](../src/syntax/merge_colors.hpp.skel.md#function-merge_color_spans)
- **Depends on:** [kMergeDiffMaxBytes](../src/syntax/merge_colors.hpp.skel.md#symbol-kmergediffmaxbytes)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
