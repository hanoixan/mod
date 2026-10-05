---
role: product
stamp: source 54789d69, stand-in 1192be6a
---
# module: keymap_view

The Key Bindings editor, opened with Options > Key Bindings… (or Enter on the "Key bindings" row of the User Settings panel). It replaces the old read-only Help > Key Bindings list. Like the User Settings panel it takes the whole body inside a frame. The first row is a **search** field; under it is one row per bindable command: its display name, all its keys (or `(none)`), and a `*` when they differ from the defaults.

```text
┌── Key Bindings ─────────────────────────┐
│ Search: go to                           │
│> Go to Line          Ctrl+G, F5 *       │
│                                         │
└─────────────────────────────────────────┘
 Enter: add key  Del: remove  ^R: reset  Alt+R: reset all  Esc: close
```

- **Search.** Typing filters the list at once. A command matches when every word typed is found in its display name, its internal name (`GotoLine`) or its key labels, in any case, so both "go to" and "ctrl+g" find Go to Line. Backspace deletes a character, Ctrl+Backspace clears the field, and pasted text is appended up to its first line break. The selection returns to the first row whenever the filter changes.
- **Add a key.** Enter starts a capture for the selected command: the first row reads "Press the new key for <name>   (Esc cancels)", and the next key pressed is bound with [Keymap.bind](../app/keymap.hpp.skel.md#function-bind). The capture ends whatever the outcome. A key another command has is refused with "<key> is bound to <owner>; remove it there first"; a key the command already has, plain text and other unbindable keys get their own messages. A captured key never reaches the search field.
- **Remove a key.** Delete removes the selected command's last key.
- **Reset.** Ctrl+R gives the selected command its default keys again; a default key that another command now has stays there, and the message says so. Alt+R asks App to confirm and then resets every command.
- Up, Down, Home, End, PageUp and PageDown move the selection. Esc closes (or cancels a capture). Every other key is consumed.

The editor changes the [Keymap](../app/keymap.hpp.skel.md#class-keymap) directly and reports `changed`; App saves the result into settings.json after each change. Messages go to the status line through App.

- **Owns:** the search text, the filtered row list, the selection, the scroll position and the capture state.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** optional — the keymap still loads from settings.json without the editor.
- **Failure modes:** a key the terminal does not deliver cannot be captured (see [input](./input.hpp.skel.md)); the menus still reach every command. After a capture the list is not re-filtered until the search text changes, so a row found by its key stays visible while that key is edited.
- **Depends on:** [Keymap](../app/keymap.hpp.skel.md#class-keymap)
- **Depends on:** [KeyEvent](./input.hpp.skel.md#symbol-keyevent)
- **Depends on:** [command_display_name](../app/commands.hpp.skel.md#function-command_display_name)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## symbol: KeymapKeyResult

`{ bool closed; bool changed; bool reset_all; std::string message; }`. `closed`: Esc closed the panel. `changed`: the keymap was edited and should be saved. `reset_all`: the user pressed Alt+R; App confirms and then calls `reset_all()`. `message`: text for the status line.

- **Access:** public.

## class: KeymapView

- **Inputs:** none at construction.
- **State changes:** `closed → open(listing | capturing) → closed`. While open it holds a pointer to the `Keymap` given to `open`.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [keymap_view (implementation)](./keymap_view.cpp.skel.md)
- **Referred by:** [keymap_view_test](../../tests/keymap_view_test.cpp.skel.md)

### function: open

- **Inputs:** a `Keymap&` that outlives the open panel.
- **Returns:** nothing.
- **State changes:** opens the panel with an empty search, every bindable command listed and the first selected. `close()` closes it; `is_open()` and `capturing()` report the state.
- **Access:** App (the `KeyBindings` command).

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** a [KeymapKeyResult](#symbol-keymapkeyresult).
- **State changes:** as the module describes: edits the search, moves the selection, starts or finishes a capture, removes a key, resets a command, or closes.
- **Access:** App, while the panel has focus.

### function: handle_paste

- **Inputs:** the bytes of a paste.
- **Returns:** nothing.
- **State changes:** appends the text up to its first line break to the search field and filters. Ignored during a capture.
- **Access:** App.

### function: reset_all

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** calls [Keymap.reset_all](../app/keymap.hpp.skel.md#function-reset_all). Called by App after the user confirmed.
- **Access:** App.

### function: render

- **Inputs:** a `Screen&`; `area`: the rectangle inside the frame.
- **Returns:** nothing.
- **State changes:** draws the search row (with the cursor at its end) or the capture request, then the list, scrolled to keep the selected row visible, inside `area` only. "no command matches" is shown for an empty list.
- **Access:** App.render.

### function: row_text

- **Inputs:** a row index of the filtered list.
- **Returns:** the row as drawn, without the selection marker. `row_count()`, `selected()`, `selected_command()` and `search()` expose the rest of the state.
- **State changes:** none.
- **Access:** `render` and tests.
