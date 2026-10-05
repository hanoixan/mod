---
role: test
stamp: source 1d79a8fc, stand-in e041125b
---
# module: list_cursor_test

`list_step` moves one row, a page or to an end, never past either end, lands an empty list on row 0, ignores other keys, and gives each key its direction; `scroll_to_show` leaves a shown selection alone and otherwise moves the window only as far as it must.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [list_step](../src/ui/list_cursor.hpp.skel.md#function-list_step)
- **Depends on:** [scroll_to_show](../src/ui/list_cursor.hpp.skel.md#function-scroll_to_show)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
