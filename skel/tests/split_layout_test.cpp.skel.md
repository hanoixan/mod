---
role: test
stamp: source 2c5346ac, stand-in 36b50ef3
---
# module: split_layout_test

`max_splits` for small and large screens; `split_rows` covering the rows exactly, even heights, the remainder to the top, one split taking everything; `focus_after_unsplit` for the top, middle and bottom split.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [max_splits](../src/app/split_layout.hpp.skel.md#function-max_splits)
- **Depends on:** [split_rows](../src/app/split_layout.hpp.skel.md#function-split_rows)
- **Depends on:** [focus_after_unsplit](../src/app/split_layout.hpp.skel.md#function-focus_after_unsplit)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
