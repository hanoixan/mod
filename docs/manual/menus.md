# Menus

The menu bar is hidden until you want it, so the text uses the whole screen. **Esc** shows it on the screen's top line, with its menus opening below it; then press a menu's underlined letter to open it, and an item's underlined letter to choose it. The chosen item is highlighted for a moment, then runs. For example, Esc F X exits, and Esc H opens Help. F10 and Alt+X do the same as Esc.

Esc opens the menu only when nothing else is open: in a prompt, a panel, a dialog or the help, Esc closes that first.

With the bar shown, Left and Right also move across the menus and Enter, Up or Down opens one; Shift+Up and Shift+Down move between [split views](documents.md#split-views), and Shift+Left goes to the [folder tree](folder-tree.md); other keys are ignored. Inside a menu, Left and Right move between menus, Up and Down between items, and Enter runs the selected item at once. The bar hides again when an item runs, on Esc, or on F10 or Alt+X again.

Press Esc three times quickly, each within a quarter of a second of the one before, to quit mod; F10 and Alt+X (or any key you bind to Show Menu) count the same, in any mix; any other key in between starts the count again. If a document has unsaved changes, mod asks about it first, as File > Exit does. The keys shown beside an item are the ones it is bound to now, including any you changed in [Key bindings](key-bindings.md).

On macOS Terminal and iTerm2, turn on "Use Option as Meta key" for Alt to reach mod.

## File

Open…, Save, Save As…, Close, Suspend, Exit. Open… opens the file as a new document, or shows it if it is already open; see [Several documents](documents.md).

## Edit

Undo, Redo, Undo History…, Next Branch, Previous Branch, Cut, Cut to Line End, Copy, Paste, Find/Replace, Find Next, Find Previous, Go to Line…. See [Undo history](undo-history.md) and [Search and replace](search.md).

## View

Line Numbers, Syntax Coloring, Word Wrap and Read Only, each a check mark, then Pin Folder Tree, which keeps the [folder tree](folder-tree.md) shown beside the views for the session, or lets it go back to showing only while you are in it. Syntax Coloring covers Markdown too: Markdown files are styled exactly when it is on. Each document has its own: they change only the document you are in (in every [split view](documents.md#split-views) of it), and the check marks show that document's. What a document starts with when it is opened is set in [Settings](settings.md) for the first three ("… on open"); documents open editable unless mod is started with [`-ro`](command-line.md). See [Read-only mode](read-only.md) for Read Only.

Split and Unsplit divide the screen into several views and remove them again; see [Split views](documents.md#split-views).

## Documents

The open documents, numbered, with a check on the one shown and `*` after any with unsaved changes. See [Several documents](documents.md).

## Options

- User Settings… opens the [settings](settings.md) panel.
- Under it, the five settings you changed most recently, for quick changes: an on/off setting flips, a choice moves to its next value, a number asks for its value.
- Key Bindings… opens the [key bindings](key-bindings.md) editor.
- Colors… opens the [Colors](colors.md) editor.

## Help

Documentation (F1) opens the manual; About mod shows the version and a few pointers. See [Help](help.md).
