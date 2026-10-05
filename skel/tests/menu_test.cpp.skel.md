---
role: test
stamp: source f2818577, stand-in 9ec5ef54
---
# module: menu_test

The Options menu: it holds User Settings…, a separator and Key Bindings…, and Help holds Documentation (accelerator `d`) and About; settings changed recently are listed under it, newest first, numbered from 1, a boolean as a checkable item and an integer with its value ("2 Tab width: 8…"); changing a setting again moves it to the top; at most five are listed; `recent_setting_command` and `recent_setting_index` map between positions and the `RecentSetting…` commands; a digit picks a recent setting, Enter or `u` opens User Settings and `k` opens Key Bindings, and Up, Down and End step over the separator; a menu shows the key a command is bound to now, not its default; View has a checkable Word Wrap item with the accelerator `w`; File holds only Open, Save, Save As and Exit; rendering shows a check mark on a recent boolean exactly when the `checked` callback says it is on. File > Suspend shows Ctrl+T, and Edit > Cut to Line End (accelerator `l`) follows Cut and shows Ctrl+K; View has a checkable Read Only item with the accelerator `r`; The Documents menu after View lists documents in order, numbered, with `*` and a check from the item itself; a digit or Enter gives `ShowDocument` and its position; past nine items have no digit; File has Close and The bar is hidden until shown: F10 and Alt+X are bound to ShowMenu and no other Alt+letter opens a menu; drawn at the given row (the top) with its menu opening downward below it; armed, a menu's letter opens it, Left and Right move the highlight, Enter and Down open, Esc hides; open, an item's letter chooses it and leaves it flashing (shown selected, keys ignored) until hidden, Enter runs at once, Esc hides, Alt+letter does nothing; `open` shows a menu directly; `help_hint` is always "Esc h: help". A recent choice setting is listed with its name and no ellipsis. View lists Line Numbers, Syntax Coloring, Word Wrap and Read Only, and no ToggleMarkdown command exists. View ends with a separator, Split (p) and Unsplit (u). View lists Pin Folder Tree after Read Only.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; modified times come from an injected clock and the settings are never written.
- **Depends on:** [MenuBar](../src/ui/menu.hpp.skel.md#class-menubar)
- **Depends on:** [Settings.recent](../src/app/settings.hpp.skel.md#function-recent)
- **Depends on:** [CommandId](../src/app/commands.hpp.skel.md#symbol-commandid)
- **Depends on:** [Keymap](../src/app/keymap.hpp.skel.md#class-keymap)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [MenuBar.set_documents](../src/ui/menu.hpp.skel.md#function-set_documents)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
