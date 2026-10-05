---
role: test
stamp: source 8ffb9505, stand-in 4f3517d0
---
# module: history_preview_test

The preview shows a node's text read-only at the same lines (though offsets moved), a row without a node shows the current state, and ending puts the text, the view's place and its preview state back; Tab gives the text the focus, whose Up, Down, Page Down, End and Home scroll it by lines and pages, other keys pass, and ending gives the focus back to the pane. The preview shows where the selected step changed the text, two rows clear of the edges, moving from what is on screen; a step at the very top shows the top; ending goes back.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [HistoryPreview](../src/app/history_preview.hpp.skel.md#class-historypreview)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
