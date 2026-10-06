---
role: product
unit: ./app.hpp.skel.md
stamp: source 63a81f5e, stand-in 558a07b0
---
# module: app (implementation)

Implements [App](./app.hpp.skel.md#class-app). Input routing order for each event:

1. Menu open: `MenuBar.handle_key`.
2. Prompt open: `Prompt.handle_event`. A prompt comes before the history panel because C and P in the panel open prompts over it.
3. History panel open: `HistoryView.handle_key`. A result with `command` runs that command (`ClearHistory`, `TrimHistory` or `TogglePersistHistory`) with the panel still open.
4. A panel open: `SettingsView.handle_key`, `KeymapView.handle_key` or `DocSearchView.handle_key`; then, with the help shown, Esc puts it away; the result is applied as [App](./app.hpp.skel.md#class-app) describes.
5. Read-only mode on: Tab, Shift+Tab, Enter and Space (in a Markdown file) and Ctrl+Left / Ctrl+Right are handled as [App](./app.hpp.skel.md#class-app) describes, before the keymap.
6. `Keymap.lookup` gives a command; in read-only mode one that edits is refused. Alt+F/E/V/O/H and F10 are bindings to the `OpenMenu…` commands, so opening a menu is just a command.
7. A `Char` without Ctrl or Alt (refused in read-only mode) calls `Editor.insert_text(typing)`.
8. A `PasteEvent` (refused in read-only mode) calls [Editor.paste_text](../edit/editor.hpp.skel.md#function-paste_text), which gives every line break of the pasted text the document's line ending; while a menu, the history panel or the settings panel has focus, a paste is dropped (a prompt open over the history panel or a panel still receives it); the Key Bindings editor takes a paste into its search field.

Anything else is ignored.

- **Owns:** the routing logic and the status-message timer (5 s); the timer's deadline is part of `run`'s wait timeout, so a message clears on an idle terminal. Status messages from Document (`take_status_message`), the Sidecar's failures and the highlighter are polled once per loop iteration.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** re-entrancy when a command opens a prompt whose submit callback runs another command. Callbacks run synchronously on the main thread and must not destroy the Prompt while it is in use: closing is deferred until the end of the dispatch.
- **Depends on:** [App](./app.hpp.skel.md#class-app)
- **Depends on:** [MarkdownHighlighter](../syntax/markdown.hpp.skel.md#class-markdownhighlighter)
- **Depends on:** [SemanticHighlighter](../syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter)
- **Depends on:** [make_terminal](../platform/terminal.hpp.skel.md#function-make_terminal)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
