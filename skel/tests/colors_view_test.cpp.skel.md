---
role: test
stamp: source 40ee49b5, stand-in 973754fd
---
# module: colors_view_test

The editor lists every color name once under the five headings, in table order (the second, after `text`, Code (language servers)); headings are never selected; rows show the spec and `*` only for overridden entries; Enter asks to edit the selected name; Delete and Ctrl+R ask to reset it; Alt+R asks to reset all; Esc closes; rendering draws each sample in its look (checked cell by cell against `ColorTheme.attr`) and scrolls a short area.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [ColorsView](../src/ui/colors_view.hpp.skel.md#class-colorsview)
- **Depends on:** [ColorTheme](../src/ui/theme.hpp.skel.md#class-colortheme)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
