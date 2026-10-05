---
role: product
stamp: source 134c097c, stand-in acef1b5d
---
# module: menu

The menu bar: File, Edit, View, Documents, Options and Help. It is **hidden** until it is wanted, and the text uses the whole screen meanwhile. When shown it sits at the **top**: App draws it on the screen's top line, with its menus opening **downward** over the text (rows past the screen's bottom are cut); the top split gives up that row while it shows.

**Escape opens it**, when nothing else has the focus (a prompt, the confirm bar, a panel, a dialog or the help viewer closes on Esc first). `ShowMenu` does the same and is bound to F10 and Alt+X, for terminals and habits that prefer them. The bar opens **armed**: no menu is open yet, and the next key picks one. A menu's underlined letter opens it, and inside a menu an item's underlined letter chooses it, so `Esc F X` exits. An item chosen by its letter is highlighted for about 120 ms (**flash**) and then runs, so you see what was picked. While armed, Left and Right move the highlight across the titles and Enter, Up or Down opens the highlighted menu; any other key is ignored and the bar stays. Inside an open menu, Left and Right switch menus, Up and Down move the selection, and Enter runs the selected item at once. The bar hides when an item runs, on Esc, and on `ShowMenu` again. Alt with a letter has no meaning to the menu.

Esc pressed **three times, each within 250 ms of the one before**, quits mod (see [App](../app/app.hpp.skel.md#class-app)); since the first opens the bar and the second closes it, the third is the quit.

Menu contents:
- **File:** Open…, Save (Ctrl+S), Save As…, Close, Suspend (Ctrl+T), Exit (Ctrl+Q). Clear History… and Trim History… are in the Undo History pane (Edit > Undo History…), not in a menu.
- **Edit:** Undo (Ctrl+Z), Redo (Ctrl+Y), Undo History…, Next Branch, Previous Branch, Cut (Ctrl+X), Cut to Line End (Ctrl+K), Copy (Ctrl+C), Paste (Ctrl+V), Find/Replace (Ctrl+F), Find Next (F3), Find Previous (Shift+F3), Go to Line… (Ctrl+G).
- **View:** Line Numbers ✓, Syntax Coloring ✓ (code and Markdown), Word Wrap ✓ (on by default), Read Only ✓ (per document, off when a file is opened), Pin Folder Tree ✓ (`f`; pins the folder tree, for the session), a separator, Split (`s` is Syntax Coloring's, so Split is **p**), Unsplit (**u**).
- **Documents:** the open documents in the order they were opened, numbered 1 to 9 (the number is the accelerator; later ones have none), with a check on the shown one and `*` after one with unsaved changes. A document in read-only mode that is showing a followed link reads "guide.md → keys.md". Choosing one shows it.
- **Options:** User Settings… (opens the [SettingsView](./settings_view.hpp.skel.md#class-settingsview) panel), then the settings changed most recently, newest first and at most five, for quick changes: each is numbered 1 to 5 (the number is its accelerator), a boolean is a checkable item showing its state ("1 Line numbers at startup"), an integer shows its value ("2 Tab width: 8…"), and a choice its name ("3 Terminal mode: vt100"; choosing it steps to the next name). The list comes from [Settings.recent](../app/settings.hpp.skel.md#function-recent), so it survives restarts. After the recent settings come a separator, Key Bindings… (opens the [KeymapView](./keymap_view.hpp.skel.md#class-keymapview) editor) and Colors… (opens the [ColorsView](./colors_view.hpp.skel.md#class-colorsview) editor). There is no fixed Tab Width… item any more: the tab width is a setting like the others.
- **Help:** Documentation (F1: the help screen), About mod. The key bindings list moved to Options > Key Bindings…, where it is also editable.

- **Owns:** the menu definitions and the open or selected state.
- **Access:** public. One instance, owned by App. Main thread.
- **Required:** always.
- **Failure modes:** Alt not reaching the application (Esc and F10 still work). In macOS Terminal and iTerm2, "Option as Meta" must be enabled, which is documented in Help; otherwise Option produces characters. F10 reaches the menu where Alt+M does not. A terminal narrower than the bar truncates the labels.
- **Depends on:** [CommandId](../app/commands.hpp.skel.md#symbol-commandid)
- **Depends on:** [Keymap.binding_label](../app/keymap.hpp.skel.md#function-binding_label)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## function: help_hint

- **Inputs:** a `const Keymap&`.
- **Returns:** the status line's idle hint, `"Esc h: help"`: Esc always opens the menu, whatever `ShowMenu` is bound to.
- **State changes:** none.
- **Access:** App, for the status line's center; tests.

## symbol: MenuId

The menus of the bar, left to right: `file`, `edit`, `view`, `documents`, `options`, `help`; its value is the menu's index in the bar's menus.

- **Access:** App, the menu's tests.

## symbol: MenuItem

`{ std::string label; char accel; CommandId command; bool checkable; int arg; std::optional<bool> checked; }`, built with a four-argument constructor (`arg` −1, `checked` unset); a default-constructed item is a separator, with an empty label. `arg` is the document position of a `ShowDocument` item. `checked`, when set, is the item's own check mark and the render callback is not asked. `DocumentMenuEntry` is `{ std::string label; bool dirty; bool shown; }`. A menu is `Menu { std::string title; char accel; std::vector<MenuItem> items; }`; `menus()` returns the table, for tests.

- **Access:** public.

## class: MenuBar

- **Inputs:** `keymap`: a `const Keymap&`, for the key labels shown beside the items; they are read at every draw, so a menu shows the key a command is bound to now. The menus themselves are the static definition above, built in by the implementation.
- **State changes:** `hidden | armed(menu_index) | open(menu_index, item_index)`. `is_visible()` is true in `armed` and `open`; `is_open()` only in `open`.
- **Owns:** the state.
- **Access:** App routes keys here while it is open.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [menu (implementation)](./menu.cpp.skel.md)
- **Referred by:** [menu_test](../../tests/menu_test.cpp.skel.md)
- **Referred by:** [menus.md](../../docs/manual/menus.md.skel.md)

### function: open

- **Inputs:** `menu`: a `MenuId`. (The bar's own keys, Left, Right and the menus' letters, open a menu by its index through a private `open_at`, clamped to the menus there are.)
- **Returns:** nothing.
- **State changes:** shows the bar and opens that menu with its first item selected.
- **Access:** App, for the `OpenMenu…` commands (which have no default keys).

### function: show

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** `hidden` → `armed`, with the first title highlighted. `hide()` goes to `hidden` from any state.
- **Access:** App, for `ShowMenu`.

### function: close

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** the same as `hide()`: an open menu never falls back to `armed`.
- **Access:** App and `handle_key`.

### function: handle_key

- **Inputs:** a `KeyEvent`, while visible. Armed: a menu's letter opens it, Left and Right move the highlight, Enter and Down open the highlighted menu, Esc hides. Open: an item's letter runs it, Left and Right switch menus, Up and Down move, Home and End go to the first and last item, Enter runs, Esc hides. Any other key is consumed. `ShowMenu`'s own key is handled by App before the menu sees it.
- **Returns:** `std::optional<CommandId>`: the command to run, after which the bar is hidden. Otherwise the key is consumed as navigation.
- **State changes:** navigation and close.
- **Access:** App.

### function: set_recent_settings

- **Inputs:** a `const Settings&`.
- **Returns:** nothing.
- **State changes:** rebuilds the Options menu: User Settings…, then one item per row of `settings.recent(5)`, with the commands `RecentSetting1` … `RecentSetting5` and the accelerators `1` … `5`, then a separator, Key Bindings… and Colors…. A separator is drawn as a rule and is skipped by Up and Down. Every other menu is static. Whether a recent boolean is checked is answered at draw time by `render`'s `checked` callback, like the View toggles.
- **Access:** App, at startup and after every settings change.
- **Depends on:** [Settings.recent](../app/settings.hpp.skel.md#function-recent)

### function: set_documents

- **Inputs:** the open documents as `DocumentMenuEntry`s, in the Documents menu's order.
- **Returns:** nothing.
- **State changes:** rebuilds the Documents menu as the module describes: one checkable `ShowDocument` item per entry, `arg` its position, `checked` its `shown`. `chosen_arg()` returns the `arg` of the item most recently chosen with Enter or its accelerator.
- **Access:** App, before each frame is drawn.

### function: render

- **Inputs:** a `Screen&`; `bar_row`: the row the bar is drawn on (the screen's top line; a row off the screen draws nothing); `checked`: a callback `bool(CommandId)` for check marks.
- **Returns:** nothing.
- **State changes:** draws the bar on `bar_row`, and the open menu upward from the row above it, as an overlay over the text area (rows above row 0 are not drawn). While flashing, the chosen item is drawn selected. `flashing()` says whether an item chosen by its letter is waiting to run; `hide()` ends it.
- **Access:** App.render.
