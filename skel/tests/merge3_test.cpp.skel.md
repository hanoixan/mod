---
role: test
stamp: source fe33cef1, stand-in 612459e1
---
# module: merge3_test

Line indexing in pieces and the last line without a line feed; diff_lines on fixed cases and 400 random pairs, each rebuilding b and minimal against an LCS table, with its progress reaching a's lines; stopping; merge3's kinds (theirs alone, ours alone, the same change, an overlap, inserts at one place, adjacent changes) and 300 random non-overlapping edits merged to the expected text.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; seeded randomness.
- **Depends on:** [diff_lines](../src/edit/merge3.hpp.skel.md#function-diff_lines)
- **Depends on:** [merge3](../src/edit/merge3.hpp.skel.md#function-merge3)
- **Depends on:** [LineIndexer](../src/edit/merge3.hpp.skel.md#class-lineindexer)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
