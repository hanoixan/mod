---
role: test
stamp: source 24157db7, stand-in 9f8f013d
---
# module: text_field_test

[TextField](../src/ui/text_field.hpp.skel.md#class-textfield): typing inserts at the cursor; Backspace and Delete remove a whole grapheme cluster (a flag, `e` plus an accent); Left, Right, Home and End move; Left at the start and Right at the end return false (so a dialog can move focus) and leave the text alone; Tab, Enter, Escape, Up, Down and Ctrl keys return false; `insert` drops line breaks; `set_text` puts the cursor at the end; `render` scrolls so the cursor stays inside a narrow field, pads to the width and sets the screen cursor only when focused.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks.
- **Depends on:** [TextField](../src/ui/text_field.hpp.skel.md#class-textfield)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
