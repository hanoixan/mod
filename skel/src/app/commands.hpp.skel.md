---
role: product
stamp: source c969f8a6, stand-in 539e03db
---
# module: commands

The closed set of user-level commands. Keys (through [Keymap](./keymap.hpp.skel.md#class-keymap)) and menu items (through [MenuBar](../ui/menu.hpp.skel.md#class-menubar)) both resolve to a `CommandId`, so every action has a single implementation in [App.run_command](./app.hpp.skel.md#function-run_command).

- **Owns:** the `CommandId` enumeration and its metadata table.
- **Access:** public.
- **Required:** always.
- **Failure modes:** none at runtime. One drift risk: a command added to the enum but missing from the table, so `all_commands` is checked at compile time to have the same length as the enum.
- **Depends on:** none
- **Unknowns:** none

## symbol: CommandId

`enum class CommandId`. The values are:

- **Files:** `Open` (a new document, or a switch to one already open), `Save`, `SaveAs`, `CloseDocument` (File > Close), `Suspend` (stops mod as a shell job), `ClearHistory`, `TrimHistory`, `Exit`. `ClearHistory` and `TrimHistory` have no menu item: they are run from the Undo History pane (C and P), and can be given keys in the Key Bindings editor.
- **History:** `Undo`, `Redo`, `UndoHistory` (opens the [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) panel), `NextBranch`, `PrevBranch`.
- **Clipboard:** `Cut`, `CutToLineEnd`, `Copy`, `Paste`.
- **Search and navigation:** `Find`, `FindNext`, `FindPrev`, `GotoLine`.
- **View toggles:** `ToggleLineNumbers`, `ToggleSyntax` (also Markdown styling), `ToggleWordWrap`, `ToggleReadOnly`, `PinFolderTree` ("Pin Folder Tree": pins or unpins the folder tree, for the session). **Views:** `Split`, `Unsplit` (no default keys).
- **Options:** `Colors` (opens the [ColorsView](../ui/colors_view.hpp.skel.md#class-colorsview) editor), `KeyBindings` (opens the [KeymapView](../ui/keymap_view.hpp.skel.md#class-keymapview) editor), `UserSettings` (opens the [SettingsView](../ui/settings_view.hpp.skel.md#class-settingsview) panel), and `RecentSetting1` … `RecentSetting5`, the Options menu's recent-settings items, newest first. The five are consecutive, so [recent_setting_command](#function-recent_setting_command) and [recent_setting_index](#function-recent_setting_index) convert between a position and the command by arithmetic. Their menu labels are built by MenuBar, so their `menu_label` is empty.
- **Documents:** `ShowDocument`, every item of the Documents menu; which document comes with the menu item (`MenuBar.chosen_arg`), so it cannot have keys.
- **Help:** `ShowHelp` (F1, Help > Documentation: the help screen), `About`.
- **Motions:** `MoveLeft`, `MoveRight`, `MoveWordLeft`, `MoveWordRight`, `MoveUp`, `MoveDown`, `MoveLineStart`, `MoveLineEnd`, `MovePageUp`, `MovePageDown`, `MoveDocStart`, `MoveDocEnd`. Each motion also has a `Select…` twin for the Shift variants.
- **The view:** `ScrollLineUp`, `ScrollLineDown` (Ctrl+Up, Ctrl+Down) scroll the view a row without moving the cursor ([EditorView.scroll_rows](../ui/editor_view.hpp.skel.md#function-scroll_rows)); no menu item.
- **Editing:** `Newline`, `InsertTab` (spaces or a tab, as `tab_inserts` says), `Outdent` (Shift+Tab), `DeleteBack`, `DeleteForward`, `DeleteWordBack`, `DeleteWordForward`.
- **Menus:** `ShowMenu` (F10 and Alt+X; Esc does the same, outside the keymap) shows the hidden menu bar, armed for a menu's letter; `OpenMenuFile`, `OpenMenuEdit`, `OpenMenuView`, `OpenMenuDocuments`, `OpenMenuOptions`, `OpenMenuHelp` open one menu directly and have no default keys.

The twelve `Move…` values come in the order of [Motion](../edit/editor.hpp.skel.md#symbol-motion) (`Left, Right, WordLeft, WordRight, Up, Down, LineStart, LineEnd, PageUp, PageDown, DocStart, DocEnd`), followed by the twelve `Select…` values in the same order, so App maps a motion command to `Editor.move` by arithmetic. A last value, `Count_`, is not a command: `kCommandCount` is its value, and the table is sized by it.

- **Access:** public.
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [commands (implementation)](./commands.cpp.skel.md)
- **Referred by:** [keymap](./keymap.hpp.skel.md)
- **Referred by:** [menu](../ui/menu.hpp.skel.md)
- **Referred by:** [keymap_test](../../tests/keymap_test.cpp.skel.md)
- **Referred by:** [menu_test](../../tests/menu_test.cpp.skel.md)
- **Referred by:** [HistoryView.handle_key](../ui/history_view.hpp.skel.md#function-handle_key)

## symbol: CommandInfo

`{ CommandId id; std::string_view name; std::string_view menu_label; bool bindable; }`. `name` is the stable internal name, the one written in the `keymap` of settings.json. `bindable` is false only for the `RecentSetting…` commands and `ShowDocument`, which never have keys and are not listed in the Key Bindings editor.

- **Access:** public.
- **Referred by:** [keymap_view_test](../../tests/keymap_view_test.cpp.skel.md)

## function: command_info

- **Inputs:** `id`.
- **Returns:** `const CommandInfo&`.
- **State changes:** none.
- **Access:** MenuBar, Keymap, KeymapView and logging.

## function: all_commands

- **Inputs:** none.
- **Returns:** `std::span<const CommandInfo>`.
- **State changes:** none.
- **Access:** Keymap and KeymapView.

## function: command_display_name

- **Inputs:** `id`.
- **Returns:** a name to show the user: the menu label without its trailing "…", or, for a command with no menu label, its internal name with a space before each capital ("Move Word Left").
- **State changes:** none.
- **Access:** KeymapView and Keymap.
- **Referred by:** [keymap_view](../ui/keymap_view.hpp.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)

## function: command_by_name

- **Inputs:** `name`: an internal command name, as written in the `keymap` of settings.json.
- **Returns:** `std::optional<CommandId>`: the command whose `name` is exactly `name` (case matters); `nullopt` when there is none.
- **State changes:** none.
- **Access:** Keymap, when it reads the user's overrides.

## symbol: kRecentSettingCount

`inline constexpr std::size_t kRecentSettingCount = 5;`: how many recently changed settings the Options menu lists, and the number of `RecentSetting…` commands.

- **Access:** public.

## function: recent_setting_command

- **Inputs:** `index`: a position in the recent-settings list, 0 for the newest, below `kRecentSettingCount`.
- **Returns:** the `RecentSetting…` command for that position.
- **State changes:** none.
- **Access:** MenuBar, when it rebuilds the Options menu.

## function: recent_setting_index

- **Inputs:** a `CommandId`.
- **Returns:** `std::optional<std::size_t>`: the position (0 is the newest) when the command is one of the `RecentSetting…` commands; `nullopt` for any other command.
- **State changes:** none.
- **Access:** App, to run a recent-settings item and to answer whether it is checked.

## function: command_edits

- **Inputs:** a `CommandId`.
- **Returns:** whether the command changes the document's text or its history: `Undo`, `Redo`, `UndoHistory`, `NextBranch`, `PrevBranch`, `ClearHistory`, `TrimHistory`, `Cut`, `CutToLineEnd`, `Paste`, `Newline`, `InsertTab`, `Outdent` and the four deletes. Everything else, including the motions, the selection motions, Copy, Find and the file commands, does not.
- **State changes:** none.
- **Access:** App, which refuses these in read-only mode.
