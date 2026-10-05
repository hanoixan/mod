---
role: product
stamp: source 973c74d8, stand-in 257c878e
---
# module: keymap

The key-to-command bindings. They start as the built-in **defaults** in the tables below, and the user can change them: in the Key Bindings editor ([KeymapView](../ui/keymap_view.hpp.skel.md#class-keymapview), Options > Key Bindings…) or by hand in `settings.json`. Only the user's **changes** are stored, under `keymap`, as `{"<CommandName>": ["<key>", …]}`: an entry replaces that command's whole key list, and an empty list leaves the command with no key. A command that is not listed follows the defaults, so a later change to the defaults still reaches it.

Rules that hold for every edit:

- A key belongs to at most one command. Binding a key that another command has is **refused**; the user removes it there first. Nothing is ever taken silently by the editor.
- A command may have several keys (Home and Ctrl+A both move to the line start). The first one is the label shown in the menus.
- Plain text (a character without Ctrl or Alt) and Esc cannot be bound: text is inserted, and Esc closes things everywhere.
- Keys inside menus, prompts and panels are fixed; only the bindings of commands are editable.

Key labels are the text form of a key, in the file and on screen: modifiers in the order `Ctrl+`, `Alt+`, `Shift+`, then the key name (`Enter`, `Tab`, `Backspace`, `Delete`, `Insert`, `Up`, `Down`, `Left`, `Right`, `Home`, `End`, `PageUp`, `PageDown`, `F1` … `F12`, `Space`) or a single character, a letter in upper case. `Shift+Tab` is its own key.

Default bindings requested at the start:

| Key | Command |
|---|---|
| Ctrl+S | Save |
| Ctrl+Home / Ctrl+End | MoveDocStart / MoveDocEnd |
| Ctrl+Z / Ctrl+Y | Undo / Redo |
| Ctrl+C / Ctrl+V | Copy / Paste |
| Shift+arrows | Select… motions |
| Ctrl+F | Find (Tab inside the bar switches to replace) |
| Ctrl+A / Ctrl+E | MoveLineStart / MoveLineEnd |
| Ctrl+Left / Ctrl+Right | MoveWordLeft / MoveWordRight (with Shift: select) |
| Alt+X | ShowMenu: shows the hidden menu bar, as Esc does; the next letter picks a menu (the request replaced the earlier Alt+F / Alt+E / Alt+V / Alt+O / Alt+H keys, and later Alt+M) |

Bindings decided after the request:

| Key | Command |
|---|---|
| Ctrl+Q | Exit (through the dirty check). Esc never quits. |
| Ctrl+X | Cut |
| F3 / Shift+F3 | FindNext / FindPrev |
| Ctrl+G | GotoLine |
| Ctrl+T | Suspend: stops mod as a shell job, like Ctrl+Z in a shell; `fg` brings it back. Ctrl+Z stays Undo. |
| Ctrl+K | CutToLineEnd |
| F1 | ShowHelp: the help screen |
| F10 | ShowMenu, as Esc and Alt+X |

Commands with no default key: SaveAs, CloseDocument, ClearHistory, TrimHistory, TogglePersistHistory, UndoHistory, NextBranch, PrevBranch, UserSettings, KeyBindings, Colors, the View toggles, About and the six `OpenMenu…` commands (bindable for anyone whose terminal passes Alt) (the RecentSetting commands can never have keys). Save As and UndoHistory (Edit > Undo History…) are menu-only by decision; the others have no key because none was specified.

Conventional bindings with no conflict: arrows, Home/End (with Shift: select), PageUp/PageDown (with Shift: select), Shift+Ctrl+Home/End, Enter, Tab (InsertTab: inserts a tab character), Backspace and Delete, and Ctrl+Backspace / Ctrl+Delete for DeleteWordBack / DeleteWordForward (the word deletes [input](../ui/input.hpp.skel.md) describes). Tab inserts an indent (`InsertTab`) and Shift+Tab removes one (`Outdent`).

Note: Ctrl+A here is *line start* (Emacs-style), as requested, not "select all".

- **Owns:** the default table and the live bindings.
- **Access:** public. One instance, owned by App.
- **Required:** always.
- **Failure modes:** a binding the terminal cannot deliver; see [input](../ui/input.hpp.skel.md). Every command is also reachable from the menu, except the motions, which have unmodified fallbacks. Some terminals keep F10 for their own menu (GNOME Terminal by default) or send no Shift+F3 sequence; Alt+F and Edit > Find Previous remain. A hand-written label for a key the terminal reports differently (Ctrl+M arrives as Enter, Ctrl+I as Tab, Ctrl+H as Ctrl+Backspace) is accepted but never matches; the editor's key capture records what the terminal really sends, so it cannot produce one. A user who removes the keys of a command they still need can restore them with the editor's reset, which stays reachable from the menu (Alt+O is itself a binding; F10 is a second way in, and deleting `keymap` from settings.json always works).
- **Depends on:** [CommandId](./commands.hpp.skel.md#symbol-commandid)
- **Depends on:** [KeyEvent](../ui/input.hpp.skel.md#symbol-keyevent)
- **Depends on:** [Json](../syntax/json.hpp.skel.md#class-json)

- **Unknowns:** none

## symbol: KeyBinding

`{ KeyEvent key; CommandId command; }`: one key bound to one command. `key` is in its normalized form.

- **Access:** public.

## symbol: BindOutcome

`{ Kind kind; std::optional<CommandId> owner; }` with `enum Kind { bound, already_bound, taken, not_bindable }`: what [bind](#function-bind) did. `owner` is set for `taken` (the command that has the key) and for `already_bound`.

- **Access:** public.

## class: Keymap

- **Inputs:** none. A new Keymap holds the defaults.
- **State changes:** the live bindings change through `bind`, `unbind`, `reset`, `reset_all` and `apply_overrides`. Invariant: no key is bound twice. Keys are stored and compared in their normalized form (`normalized`): letters in lower case, with Shift dropped from an Alt+letter, and Ctrl with a letter or one of `@\]^_` and space as `Key::CtrlLetter` however the terminal reported it.
- **Owns:** the bindings.
- **Access:** App (lookup, loading and saving), MenuBar (labels) and KeymapView (editing).
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [keymap (implementation)](./keymap.cpp.skel.md)
- **Referred by:** [keymap_view](../ui/keymap_view.hpp.skel.md)
- **Referred by:** [input_test](../../tests/input_test.cpp.skel.md)
- **Referred by:** [keymap_test](../../tests/keymap_test.cpp.skel.md)
- **Referred by:** [keymap_view_test](../../tests/keymap_view_test.cpp.skel.md)
- **Referred by:** [menu_test](../../tests/menu_test.cpp.skel.md)
- **Referred by:** [fuzz_config](../../fuzz/fuzz_config.cpp.skel.md)

### function: lookup

- **Inputs:** a `KeyEvent`.
- **Returns:** `std::optional<CommandId>`. Plain text characters (a `Char` without Alt or Ctrl, with or without Shift) return `nullopt` and are inserted. Letters compare case-insensitively, so Alt+Shift+X shows the menu like Alt+F.
- **State changes:** none.
- **Access:** App.

### function: bindings

- **Inputs:** none.
- **Returns:** the live bindings as `std::span<const KeyBinding>`.
- **State changes:** none.
- **Access:** tests.

### function: key_label

- **Inputs:** a `KeyEvent`.
- **Returns:** its label, as described in the module: `Ctrl+S`, `Shift+F3`, `Alt+X`, `Ctrl+Space`.
- **State changes:** none. Static.
- **Access:** `binding_label`, KeymapView, `overrides`.

### function: parse_key

- **Inputs:** a label.
- **Returns:** `std::optional<KeyEvent>`: the normalized key the label names, the exact inverse of `key_label`; `nullopt` for text that names no key. Modifier and key names are accepted in any case and modifiers in any order. A single character with no modifier parses (it is a key) but is not `bindable`.
- **State changes:** none. Static.
- **Access:** `apply_overrides` and tests.

### function: bindable

- **Inputs:** a `KeyEvent`.
- **Returns:** false for Esc, for plain text (a `Char` without Ctrl or Alt) and for an undecodable character; true otherwise.
- **State changes:** none. Static.
- **Access:** `bind`, `apply_overrides`.

### function: binding_label

- **Inputs:** a `CommandId`.
- **Returns:** the label of the command's first live binding, such as `"Ctrl+S"`, or empty.
- **State changes:** none.
- **Access:** MenuBar. Because it reads the live bindings, a menu shows the key a command has now.
- **Referred by:** [menu](../ui/menu.hpp.skel.md)

### function: keys_for

- **Inputs:** a `CommandId`.
- **Returns:** the command's keys, in binding order.
- **State changes:** none.
- **Access:** KeymapView, `overrides`.

### function: bind

- **Inputs:** a `CommandId` and a `KeyEvent`.
- **Returns:** a [BindOutcome](#symbol-bindoutcome): `bound`; `already_bound` (the command has that key); `taken`, with the owning command, when another command has it; `not_bindable`.
- **State changes:** on `bound`, the key is added after the command's other keys. Every other outcome changes nothing.
- **Access:** KeymapView.

### function: unbind

- **Inputs:** a `CommandId` and one of its keys.
- **Returns:** whether the key was removed.
- **State changes:** removes that binding.
- **Access:** KeymapView.

### function: reset

- **Inputs:** a `CommandId`.
- **Returns:** the default keys of the command that another command now has; they stay where they are.
- **State changes:** the command gets its default keys again, except the returned ones.
- **Access:** KeymapView.

### function: overrides

- **Inputs:** none.
- **Returns:** a JSON object with one member per command whose keys differ from its defaults, in command order: the command's name and the list of its key labels (empty for a command left with no key). Empty when nothing differs.
- **State changes:** none.
- **Access:** App, to save the keymap into [settings_file](../../infra/storage.iac.skel.md#resource-settings_file).
- **Depends on:** [settings_file](../../infra/storage.iac.skel.md#resource-settings_file)

### function: apply_overrides

- **Inputs:** the `keymap` value from settings.json.
- **Returns:** one warning per entry that was skipped: "keymap must be an object", "unknown command <name>", "<name> cannot have keys", "<name>: the keys must be a list", "<name>: a key must be text", "<name>: <label> is not a key", "<name>: <label> cannot be bound", "<name>: <label> is already bound to <other>".
- **State changes:** starts from the defaults. Every valid entry first empties its command's key list; then the listed keys are added in file order. A listed key that a command *without* an entry has by default moves to the listed command: the user's choice beats a default (this is what makes a file written before a default changed keep working). A listed key that an earlier entry already claimed is skipped with a warning. Everything valid still applies when something else is skipped.
- **Access:** App, once during `starting`.
- **Referred by:** [startup](./startup.hpp.skel.md)

### function: default_bindings

- **Inputs:** none. Static.
- **Returns:** the built-in table as `std::span<const KeyBinding>`: the bindings in the module's tables, in the order that decides which key is a command's menu label.
- **State changes:** none.
- **Access:** tests, and `reset`, `reset_all`, `is_default` and `overrides` inside the class.
- **Referred by:** [key-bindings.md](../../docs/manual/key-bindings.md.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)

### function: normalized

- **Inputs:** a `KeyEvent`.
- **Returns:** the form bindings are stored and compared in: an upper-case letter becomes lower case, Shift is dropped from a letter that is not a Ctrl combination, and Ctrl with a letter or one of `@\]^_` and space becomes `Key::CtrlLetter` however the terminal reported it.
- **State changes:** none. Static.
- **Access:** `owner`, `bind`, `unbind`, `parse_key`, and KeymapView (to label a captured key).

### function: owner

- **Inputs:** a `KeyEvent`.
- **Returns:** `std::optional<CommandId>`: the command the key is bound to, if any. Unlike `lookup`, it answers for plain text too (always `nullopt`, since text is never bound).
- **State changes:** none.
- **Access:** `lookup`, `bind`, `reset`, `apply_overrides`, and KeymapView (to name the command a default key stayed with).

### function: is_default

- **Inputs:** a `CommandId`.
- **Returns:** whether the command's keys are exactly its default keys, in the same order.
- **State changes:** none.
- **Access:** `overrides`, and KeymapView (the `*` on a changed row).

### function: reset_all

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** the live bindings become the default table again.
- **Access:** KeymapView, after App has confirmed with the user; `apply_overrides`, which starts from the defaults.
