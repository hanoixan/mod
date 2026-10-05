---
role: test
stamp: source 42dae0d6, stand-in b270dbfb
---
# module: reading_layout_test

`locate` on visible text and on hidden marks; Left/Right skipping hidden marks; Up/Down keeping the column and passing over blank and rule lines; Home/End; DocStart/DocEnd; `visible_text` of a wrapped paragraph and of a list.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [ReadingLayout](../src/ui/reading_layout.hpp.skel.md#class-readinglayout)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
