---
role: test
stamp: source 03e63f39, stand-in 696eff63
---
# module: keymap_test

Every default binding's label parses back to the same key; `parse_key` accepts names and modifiers in any order and case, gives `Shift+Tab`, `Ctrl+Space`, Ctrl+Alt and Ctrl+Shift letters their normalized keys, rejects text that names no key, and round-trips labels of keys outside the default table; plain text and Esc are not bindable; `bind` adds a key, `lookup` follows it, the old key stays and the first key stays the menu label; a key another command has is refused with its owner and nothing changes, as are a key the command already has and an unbindable key; `unbind` removes one key and frees it for another command; `reset` restores a command's default keys except those another command now has, which it returns, and `reset_all` restores everything; `overrides` lists only the commands that differ and round-trips through `apply_overrides`, which starts from the defaults; an override replaces the command's whole list and beats a default owner of the same key; bad override entries are skipped with one warning each while the rest apply; command display names, the `bindable` flag and `command_by_name`. Ctrl+T is Suspend and Ctrl+K CutToLineEnd by default, while Ctrl+Z and Ctrl+Y stay Undo and Redo. Which commands count as edits for read-only mode. F1 opens the help.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [Keymap](../src/app/keymap.hpp.skel.md#class-keymap)
- **Depends on:** [CommandId](../src/app/commands.hpp.skel.md#symbol-commandid)
- **Depends on:** [Json](../src/syntax/json.hpp.skel.md#class-json)
- **Depends on:** [KeyEvent](../src/ui/input.hpp.skel.md#symbol-keyevent)
- **Depends on:** [command_display_name](../src/app/commands.hpp.skel.md#function-command_display_name)
- **Depends on:** [command_edits](../src/app/commands.hpp.skel.md#function-command_edits)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
