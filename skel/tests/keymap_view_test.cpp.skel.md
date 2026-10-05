---
role: test
stamp: source d8c9b1a0, stand-in 85ebfcc0
---
# module: keymap_view_test

The editor lists every bindable command with its keys and never a `RecentSetting` command; typing filters by display name, internal name and key label in any case, with every word required, and an empty result selects nothing; pasted text goes into the search field up to its first line break; Up, Down, Home, End and the page keys move the selection, which returns to the top when the filter changes; Enter captures the next key and adds it to the selected command, marks the row with `*`, and never lets the captured key into the search; a captured key another command has is refused with the owner's name, as are a key the command already has and plain text; Esc cancels a capture without closing and then closes; Delete removes the last key and says so when none is left; Ctrl+R resets the selected command and Alt+R only asks, with `reset_all` doing the work; a reset names a default key that stays with another command; rendering draws the search row, the rows, the highlighted selection and the capture line, nothing outside the area, and scrolls a long list.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [KeymapView](../src/ui/keymap_view.hpp.skel.md#class-keymapview)
- **Depends on:** [Keymap](../src/app/keymap.hpp.skel.md#class-keymap)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [CommandInfo](../src/app/commands.hpp.skel.md#symbol-commandinfo)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [attr_for](../src/ui/theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
