---
role: test
stamp: source 062534fb, stand-in 9add1626
---
# module: view_mode_test

Tab and Shift+Tab step through the links of a Markdown text, wrap at both ends, never stop on the link they start in, and find nothing without links; the link under the cursor, its end excluded; resolving a relative file, a file with an anchor, a path that climbs a folder, a percent-escaped name, a local anchor that exists and one that does not, a web address and a mailto address; the trail: visiting, going back and forward with each entry's position kept, both ends refusing, and a new visit after going back dropping the forward entries while the base keeps its newer position.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [ViewModeNav](../src/app/view_mode.hpp.skel.md#class-viewmodenav)
- **Depends on:** [scan_markdown](../src/syntax/markdown.hpp.skel.md#function-scan_markdown)
- **Depends on:** [PieceTree](../src/text/piece_tree.hpp.skel.md#class-piecetree)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
