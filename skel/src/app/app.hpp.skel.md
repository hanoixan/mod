---
role: product
untested: needs a real terminal and every component wired together; it is exercised by running mod, and no automated test drives it
stamp: source 27a13f97, stand-in fefec812
---
# module: app

The application object: it owns every long-lived component, runs the single-threaded event loop, routes input (menu, then prompt, then history panel, then an open panel, then keymap, then text insertion), executes commands, and renders frames.

- **Owns:** the [Terminal](../platform/terminal.hpp.skel.md#class-terminal), [Screen](../ui/screen.hpp.skel.md#class-screen), [InputDecoder](../ui/input.hpp.skel.md#class-inputdecoder), [EventQueue](../util/event_queue.hpp.skel.md#class-eventqueue), [Document](../edit/document.hpp.skel.md#class-document), [Editor](../edit/editor.hpp.skel.md#class-editor), [Clipboard](../edit/clipboard.hpp.skel.md#class-clipboard), [Searcher](../search/search.hpp.skel.md#class-searcher), the active [Highlighter](../syntax/highlight.hpp.skel.md#class-highlighter), [LanguageConfig](../syntax/language_config.hpp.skel.md#class-languageconfig), [EditorView](../ui/editor_view.hpp.skel.md#class-editorview), [MenuBar](../ui/menu.hpp.skel.md#class-menubar), [HistoryView](../ui/history_view.hpp.skel.md#class-historyview), [SettingsView](../ui/settings_view.hpp.skel.md#class-settingsview), [KeymapView](../ui/keymap_view.hpp.skel.md#class-keymapview), [Prompt](../ui/prompt.hpp.skel.md#class-prompt), [Keymap](./keymap.hpp.skel.md#class-keymap), [Settings](./settings.hpp.skel.md#class-settings), the view toggles, and the status message.
- **Access:** public. A single instance constructed by [main](../main.cpp.skel.md#function-main).
- **Required:** always.
- **Unknowns:** none
- **Failure modes:** an exception escaping a command handler. It is caught in `run`, logged and shown on the status line, and the loop continues: the document is never lost to a UI bug. On exit with a dirty document, a `confirm` prompt asks Save / Discard / Cancel; Save goes through [save_document](#function-save_document), so it can itself ask about in-place writing.
- **Depends on:** [Terminal](../platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [Screen](../ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [InputDecoder](../ui/input.hpp.skel.md#class-inputdecoder)
- **Depends on:** [EventQueue](../util/event_queue.hpp.skel.md#class-eventqueue)
- **Depends on:** [Document](../edit/document.hpp.skel.md#class-document)
- **Depends on:** [Editor](../edit/editor.hpp.skel.md#class-editor)
- **Depends on:** [Clipboard](../edit/clipboard.hpp.skel.md#class-clipboard)
- **Depends on:** [Searcher](../search/search.hpp.skel.md#class-searcher)
- **Depends on:** [Highlighter](../syntax/highlight.hpp.skel.md#class-highlighter)
- **Depends on:** [LanguageConfig](../syntax/language_config.hpp.skel.md#class-languageconfig)
- **Depends on:** [EditorView](../ui/editor_view.hpp.skel.md#class-editorview)
- **Depends on:** [MenuBar](../ui/menu.hpp.skel.md#class-menubar)
- **Depends on:** [Prompt](../ui/prompt.hpp.skel.md#class-prompt)
- **Depends on:** [HistoryView](../ui/history_view.hpp.skel.md#class-historyview)
- **Depends on:** [history_pane_width](../ui/history_view.hpp.skel.md#function-history_pane_width)
- **Depends on:** [Keymap](./keymap.hpp.skel.md#class-keymap)
- **Depends on:** [CommandId](./commands.hpp.skel.md#symbol-commandid)
- **Depends on:** [Settings](./settings.hpp.skel.md#class-settings)
- **Depends on:** [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot)
- **Depends on:** [Workspace](./workspace.hpp.skel.md#class-workspace)
- **Depends on:** [HistoryPreview](./history_preview.hpp.skel.md#class-historypreview)
- **Depends on:** [FolderTreeView](../ui/folder_tree_view.hpp.skel.md#class-foldertreeview)
- **Depends on:** [FolderTree](./folder_tree.hpp.skel.md#class-foldertree)
- **Depends on:** [load_configuration](./startup.hpp.skel.md#function-load_configuration)
- **Depends on:** [find_doc_dir](./doc_search.hpp.skel.md#function-find_doc_dir)
- **Depends on:** [DocSearchView](../ui/doc_search_view.hpp.skel.md#class-docsearchview)
- **Depends on:** [LspServerPool](../syntax/lsp_pool.hpp.skel.md#class-lspserverpool)
- **Depends on:** [MenuBar.set_documents](../ui/menu.hpp.skel.md#function-set_documents)
- **Depends on:** [ReadOnlyNav](./read_only.hpp.skel.md#class-readonlynav)
- **Depends on:** [scan_markdown](../syntax/markdown.hpp.skel.md#function-scan_markdown)
- **Depends on:** [command_edits](./commands.hpp.skel.md#function-command_edits)
- **Depends on:** [SettingsView](../ui/settings_view.hpp.skel.md#class-settingsview)
- **Depends on:** [KeymapView](../ui/keymap_view.hpp.skel.md#class-keymapview)
- **Depends on:** [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink)
- **Depends on:** [format_progress](../util/progress.hpp.skel.md#function-format_progress)

**Progress meter.** App passes a [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink) in the `DocumentOptions` of every document it opens. While a long synchronous operation runs (a Save As copying a large history, a prune, a sidecar flush), the sink redraws **only the status line** with [format_progress](../util/progress.hpp.skel.md#function-format_progress) and flushes the Screen at once, at most every 100 ms; it never calls the full render, because the document may be mid-walk. It catches every exception itself. The next normal frame repaints the status line, so there is no "done" report.
**Settings.** Every user setting lives in [Settings](./settings.hpp.skel.md#class-settings), which keeps it in `settings.json` in the user configuration directory, **globally** (not per file or file type). A setting is changed in the User Settings panel (Options > User Settings…), or from the Options menu's recent-settings items. Every change goes through one function, `apply_setting`: it calls [Settings.set](./settings.hpp.skel.md#function-set), shows "<label>: <value>" on the status line (or "<label> not remembered: <reason>" when the file cannot be written; the value still applies for this session), makes the running session follow the new value, and rebuilds the Options menu's recent list. Following the value means: `tab_width` is passed to [EditorView](../ui/editor_view.hpp.skel.md#class-editorview) and [Editor](../edit/editor.hpp.skel.md#class-editor), which pass it to [display_width](../text/utf8.hpp.skel.md#function-display_width) (1 to 16, 4 by default; it only changes how tab characters are displayed and how display columns are counted, never the file, and it survives File > Open); `line_numbers`, `syntax_coloring` and `word_wrap` are the values a document's View options take **when it is opened** (or created), and a change to one of them also sets that option on every open document. The View options themselves (Line Numbers, Syntax Coloring, Word Wrap, Read Only) are **per document**: each document's [ViewOptions](./document_slot.hpp.skel.md#class-viewoptions) are shared by every split view of it, a View menu command changes only the focused document's (in all its views) and writes nothing, and the View menu's check marks show the focused document's. A link target shown in read-only mode uses the options of the document it was followed from.

**Word wrap.** Before every command and after every layout change (a resize, a pane opening, the gutter toggle), App gives the Editor the wrap width the view is drawing with ([EditorView.wrap_cols](../ui/editor_view.hpp.skel.md#function-wrap_cols) of the text area, or none while word wrap is off or the text area is hidden) through [Editor.set_wrap_width](../edit/editor.hpp.skel.md#function-set_wrap_width), so Up, Down, PageUp and PageDown move through the rows on screen.

**Read-only mode.** View > Read Only (`ToggleReadOnly`, no default key) turns the document into a **view**: it can be read, searched, selected and copied but not changed. While it is on:

- Every command that [edits](./commands.hpp.skel.md#function-command_edits), typed text and a paste are refused with "read-only: View > Read Only to edit" on the status line. Find works; its bar does not switch to replace.
- Arrows, PageUp/PageDown, Home/End and Ctrl+Home/End move as usual, Shift with them selects, and Ctrl+C copies.
- In a Markdown file, Tab and Shift+Tab move to the next and previous link of the file's [outline](../syntax/markdown.hpp.skel.md#function-scan_markdown), wrapping, and select its text, with the target on the status line. Enter or Space follows the link under the cursor (or the selection's start) as [ReadOnlyNav.resolve](./read_only.hpp.skel.md#function-resolve) says: a heading of the same file moves the cursor there; another file is shown **in place**, opened with `DocumentOptions.history` false so its sidecar is never touched, at the heading named after `#` if any; a web address only shows on the status line; a missing file gives "not found: <path>". In any other file Tab, Enter and Space are not special and are refused as edits.
- Ctrl+Left and Ctrl+Right go back and forward through the view's [trail](./read_only.hpp.skel.md#class-readonlynav), in any file, returning each place to the cursor and first line it was left at; at either end the status line says "no earlier page" / "no later page".
- A followed file is **transient**: it is shown in the document's place in a [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot) of its own, while the document's own slot waits unchanged; it is listed nowhere, and going back to the document, opening another file or turning read-only mode off drops it. The outline is cached for the shown document and rebuilt when its version changes.

Turning Read Only off while the document itself is shown makes it editable again and clears the trail. Turning it off while a followed file is shown opens that file as a **new document** (or switches to it if it is already open), editable, at the same place; the original document steps back one place in its trail and stays open in read-only mode. A newly opened document always starts with read-only mode off.

**Documents.** Several files can be open at once, one shown at a time. App keeps them, and the split views onto them, in a [Workspace](./workspace.hpp.skel.md#class-workspace); the focused view's [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot) and read-only state are what `shown()` and `ro()` return; every document no view shows is **parked** whole (its slot, read-only state, trail and any followed link target) and comes back exactly as it was left: cursor, scroll, selection, search query, coloring. The settings, keymap, clipboard, menus, prompt and panels belong to App and so are shared; a find bar and the history pane close when the shown document changes. Parked documents keep their files mapped, their sidecars attached and their language-server registrations, but their highlighter timers and external-change checks run only while shown; a document is checked for changes on disk as soon as it is shown again.

- The command line opens each path as a document, the first shown; a path that cannot be opened is skipped and the first failure reported on the status line. With no path, an untitled buffer.
- File > Open opens a **new** document. A file that is already open (the same real path) is switched to instead. When the shown document is an untitled buffer with nothing typed in it, the file replaces it.
- File > Close asks Save / Discard / Cancel when the document has unsaved changes, then shows the document viewed most recently before it; closing the last one leaves a new untitled buffer. A document showing a followed link closes as itself (its read-only state is dropped first).
- The Documents menu lists them as [MenuBar.set_documents](../ui/menu.hpp.skel.md#function-set_documents) describes; choosing one shows it.
- Exit asks about each document with unsaved changes in turn, showing it first: Save or Discard moves to the next, Cancel stops the exit.
- Language servers are shared through an [LspServerPool](../syntax/lsp_pool.hpp.skel.md#class-lspserverpool) that App owns, so documents of one language and project use one server.

**Help.** F1 (`ShowHelp`, Help > Documentation) opens the [help viewer](../ui/help_viewer.hpp.skel.md#class-helpviewer) on the user manual. The manual's folder is found once, with [find_doc_dir](./doc_search.hpp.skel.md#function-find_doc_dir); when it is not found, an `info` overlay lists the folders tried and names `MOD_DOC_DIR`. The viewer covers the whole screen above the status line; the shown document, its view and its settings are untouched underneath, and the manual is not a document (it is not in the Documents menu, and Read Only cannot be turned off for it: View > Read Only says "the manual is always read-only"). F1 again, or Esc when no menu, prompt or panel has focus, closes the viewer and shows the document exactly as it was; the next F1 returns to the viewer's page and place, for the session.

While the viewer is open, keys go to the menu, a prompt or the search panel when one has focus, then to the viewer; a key it does not use is looked up in the keymap, where the menus, F1, User Settings, Key Bindings, Colors, Suspend and About act with the viewer still open, and any other command (Open, Save, Close, Exit, a Documents item, an edit…) closes the viewer first and then acts on the document. The status line shows `Help: <page>` on the left and the viewer's keys in the center. `/` or Ctrl+F opens the help's search panel ([DocSearchView](../ui/doc_search_view.hpp.skel.md#class-docsearchview)) over [search_docs](./doc_search.hpp.skel.md#function-search_docs) of the manual's folder, framed "Search Help"; Enter on a match shows that page at the match with [HelpViewer.show](../ui/help_viewer.hpp.skel.md#function-show).

**Coloring.** For a document that is not Markdown, App looks up its [language entry](../syntax/language_config.hpp.skel.md#function-find_for_path) and builds the highlighter as [highlight](../syntax/highlight.hpp.skel.md) describes: the syntax layer from the entry's `syntax`, the server's tokens when it has a `command` and the file is under the size cap, layered when both. View > Syntax Coloring (`ToggleSyntax`, and the `syntax_coloring` setting) switches both layers together, and Markdown styling with them: a Markdown file is styled exactly when Syntax Coloring is on. There is no separate Markdown toggle or setting; an old `markdown_formatting` member in settings.json is kept as an unknown member and not read.

**Cursor.** At startup, and whenever it changes, the `cursor_style` setting is given to the Screen with `set_cursor_style`.

**Colors.** At startup the `darkness` setting is given to the active [ColorTheme](../ui/theme.hpp.skel.md#class-colortheme) (and again, with a full redraw, whenever it changes), then the `colors` member of the settings is handed to it with `apply`; the first warning is shown as "settings.json colors: <warning>" with "(and N more)". The `Colors` command opens the [Colors editor](../ui/colors_view.hpp.skel.md#class-colorsview). Its `edit` opens a `color` prompt prefilled with the spec, which stays open with the parse error until the text parses and is then set with `ColorTheme.set`; `reset` resets that entry; `reset_all` confirms (Reset / Cancel) first. After every change App saves `ColorTheme.overrides()` with [Settings.set_raw](./settings.hpp.skel.md#function-set_raw) (removing the member when nothing differs) and invalidates the screen so everything redraws in the new look. Enter on the `colors` row of User Settings runs `Colors`.

**Terminal mode.** At startup, after [enter_raw_mode](../platform/terminal.hpp.skel.md#function-enter_raw_mode) and before the first frame, App resolves the `terminal_mode` setting: `vt100` or `xterm` as set, or for `auto` [mode_from_environment](../platform/terminal_output.hpp.skel.md#function-mode_from_environment), else a Primary Device Attributes query (`ESC[c`) waiting up to 100 ms for [a reply](../platform/terminal_output.hpp.skel.md#function-mode_from_device_attributes), else xterm. Bytes read while waiting that are not the reply are fed to the input decoder afterwards. App then calls `start_screen` with [output_for](../platform/terminal_output.hpp.skel.md#function-output_for) the mode, gives the same output to the Screen, and in vt100 mode switches the theme to its vt100 table and stops sending OSC 52 to the terminal clipboard (copies stay in the internal clipboard). Changing the setting is saved and says "terminal mode applies when mod next starts".

**Split views.** View > Split divides the focused view into an upper and a lower one showing the same document (the new one below, at the same place; the focus stays above). Splits can be split again; every split gets an even share of the screen's rows ([split_rows](./split_layout.hpp.skel.md#function-split_rows)), and each has its own status line at its bottom. A screen holds at most [max_splits](./split_layout.hpp.skel.md#function-max_splits) splits (H / 3); Split beyond that says "no room for another split". View > Unsplit removes the focused view (its document stays open) and focuses the view above it, else the one below ([focus_after_unsplit](./split_layout.hpp.skel.md#function-focus_after_unsplit)); with one view it does nothing. When the screen gets shorter, the bottom splits are removed until there are no more than H / 3; if the focused split is among them, it is left as when the focus moves (its history preview ended on its own slot, the Undo History pane closed, the find bar detached) before it goes. The focused view's status line is drawn with `StatusMark::focused` (a leading `>`), with one view or several, and the other splits' with `StatusMark::unfocused` (a darker band). A full-screen UI's own line has neither.

**The terminal's title.** After every frame, `update_title` sets the terminal's title ([TerminalOutput.append_title](../platform/terminal_output.hpp.skel.md#function-append_title)) to `mod:` and the focused view's own document's name (`[untitled]` for an untitled buffer; the document the view belongs to, never a link it is following in read-only mode), with ` *` while it has unsaved changes. It is written only when it differs from the last one written, so it changes on a focus move, Save As, another document shown in the view, the first edit and a save. A resize (which a suspended job continuing reports) forgets the last one, so it is set again after Ctrl+T. The previous title is restored on exit by the output's `leave()`.

All views share the documents: two splits may show the same document at different places, each with its own cursor, selection and scroll, and an edit in one shows at once in the other (the document, its history and its highlighter are shared; see [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot)). A document is either visible in one or more splits or parked; choosing a document (the Documents menu, Open) shows it in the focused split, sharing it when another split already shows it. Closing a document removes the other splits that show it.

Every split is a [View](./workspace.hpp.skel.md#symbol-view) in the Workspace that owns its slot and read-only state; the focused one is on screen and takes the keys. App does the bookkeeping through the Workspace (which decides by document id what is parked and what is still shown elsewhere) and, after any change of the focused view or of what it shows, runs one `on_focus_changed`: the find bar follows its searcher, its highlighter is woken, its document is checked for changes on disk, the screen is redrawn and the status line names it. With the menu bar shown (after Esc, F10 or Alt+X; escape mode), **Shift+Up and Shift+Down move the focus** to the split above or below, the bar moving with it; Esc then closes the bar and the focused split takes the keys. Only the focused split shows the menu bar, a prompt, the confirm bar and the status line's messages and hints; the others show their own status line with no message.

**Reading Markdown.** A Markdown document in read-only mode is drawn laid out for reading in every view of it: App sets [EditorView.set_reading](../ui/editor_view.hpp.skel.md#function-set_reading) with the other View options. While a view draws a [ReadingLayout](../ui/reading_layout.hpp.skel.md#class-readinglayout), the motion commands move by it ([move](../ui/reading_layout.hpp.skel.md#function-move), the sticky column kept per view), setting the editor's cursor and selection with `select_range`; and Copy follows the `read_only_copy` setting: the source, or [visible_text](../ui/reading_layout.hpp.skel.md#function-visible_text) put on the clipboard with [Clipboard.set_text](../edit/clipboard.hpp.skel.md#function-set_text). Find, Tab between links and Enter work on the source as before.

**Ending.** When the terminal's `wait` reports `terminated` (SIGTERM, SIGHUP, or a terminal that cannot be waited on), App ends at once without questions, but in order: destruction flushes every document's history to its sidecar, so edits made since the last save survive as history wherever persistence is on.

**The bands.** A prompt and a question are drawn on the screen's bottom lines, below every split's status line: from the bottom up, the prompt (its `rows_wanted`; not while the menu bar is visible) and the question (the confirm bar's rows). The menu bar, while visible, is the top band: the screen's top line (`menu_row` 0), whose row the top split gives up (then the next one down), each split keeping at least its status row; the splits below keep their place. `layout()` reserves that band: split shares are computed on the whole screen as without it, and the band's rows are then taken from the bottom split (then the next one up), each keeping at least its status row; the splits above keep their place. With one view the band sits under its status line the same way. While either band is open and several splits are shown, the other splits' text is drawn dimmed (`Screen::add_flags(kDim)`), the interaction being for the focused split. The info prompt (`PromptKind::info`) is not part of the band: it is drawn over the rows just above it, so closing it never moves the text.

**Single-view mode.** While the Undo History pane, the file dialog, a full-width panel (User Settings, Key Bindings, Colors, the help's search) or the help viewer is open, only the focused split is laid out, over the whole screen above the band, and its last row is the UI's own line (`own_line`): the UI's key hints and any message, drawn with [draw_status_line](../ui/editor_view.hpp.skel.md#function-render_status) without a document's name or position (the help's is ` Help: <page>`); no split's status line shows. The other splits come back when it closes.

**The folder tree.** A [FolderTree](./folder_tree.hpp.skel.md#class-foldertree) of the working folder in a [FolderTreeView](../ui/folder_tree_view.hpp.skel.md#class-foldertreeview) on the left, shown while it is **pinned** (`tree_pinned_`) or has the keys (`tree_focus_`, a preview included): unpinned, it shows only while you are in it and goes however the keys leave it. View > Pin Folder Tree (`PinFolderTree`) pins or unpins it for the session; the `pin_folder_tree` setting decides whether it starts pinned, and changing the setting pins or unpins it now. `layout()` gives it `kTreeWidth` (30) columns, at most a third of the screen (none under 4), the rows between the top and bottom bands, and a one-column divider (`│` in the gutter color); the views, their status lines (drawn from `Layout::left`) and their text start right of it. A full-screen UI (file dialog, panel, help, Undo History) hides it while open.

In escape mode **Shift+Left** hides the menu bar and gives the tree the keys (`tree_focus_`), which shows it if unpinned; the last focused view keeps its place, and its status line is drawn with `StatusMark::none` (no `>`) while the tree has the keys; the cursor is hidden. The tree's keys go to `FolderTreeView::handle_key`: `open` opens the file in the focused view as File > Open does ([open_new_document](#function-open_new_document)) and gives the keys back to it (a failure goes to the tree's own line); `preview` opens the file as the **preview**; `back` (Shift+Right) gives the keys back to the view and shows the menu bar again (escape mode); `leave` (Esc) gives them back to the view for editing. A key the tree does not use whose binding is a command other than a motion leaves the tree and runs it (ShowMenu's key leaves it for escape mode).

The **preview** is a [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot) of the file opened read-only and without history (its own Editor, Searcher, EditorView with read-only View options, so Markdown is laid out for reading, and a highlighter), shown as the only view right of the tree with the status line ` Preview: <name>` and `Esc: close`; the motions move through it (by what is shown when laid out), Esc closes it and the tree has the keys again. App's destructor closes it first.

**Menu bar and status line.** The menu bar is hidden until Esc or `ShowMenu`; while visible it is the screen's top line (the top band), its menus opening downward over everything below it; the top split gives up its first row while it shows. A full-screen UI's area starts below it.

**Open and Save As.** File > Open…, File > Save As… and the first save of an untitled document open the [file dialog](../ui/file_dialog.hpp.skel.md#class-filedialog) over the text (the status line stays, with the dialog's keys in its center), in the document's folder, or the working directory for an untitled one, with the file name filled in for Save As. It takes every key after the confirm bar; a paste goes to its active field. A chosen path is opened as a document, or saved to; saving onto an existing other file first asks "<name> already exists. Replace it?" (Replace / Cancel), and Cancel leaves the dialog as it was. Esc or [Cancel] closes it with nothing done.

**Questions.** Every question with choices (unsaved changes, reload, overwrite, reset all…) opens the [ConfirmBar](../ui/confirm_bar.hpp.skel.md#class-confirmbar), which App lays out in the bottom band (above any prompt) and gives every key first while it is open. A yes-or-no question goes through App's `ask(question, yes, on_yes, no = "Cancel")`: two buttons, Esc choosing the second, and `on_yes` run only for the first; questions with more buttons, or with work on both answers, open the bar directly.

**Escape.** A lone Esc goes, in order, to whatever has the focus and closes it: the menu, a prompt or the confirm bar, the history pane, a panel, the help's search panel, the help viewer. With nothing else open, Esc opens the menu. Independently of that, App counts Esc presses: the **third within one second** quits, through the usual exit flow (so a document with unsaved changes is still asked about; with none, mod quits at once). An item the menu reports as flashing is run when its ~120 ms flash ends (the loop's wait includes that deadline); keys during the flash are ignored. The status line's center gets, in order: the transient message, the open panel's or pane's key hints, else the idle hint from [help_hint](../ui/menu.hpp.skel.md#function-help_hint).

**Panels.** A full-width panel (the User Settings panel, the Key Bindings editor, the Colors editor or the help's search panel, never two at once: App names the open one with `panel()`, an enum `Panel { none, settings, keymap, colors, doc_search }`, and every way of opening one, `open_doc_search` included, first calls `close_panels`, as do opening the history pane and the file dialog) takes the whole body from row 0 to the status line, inside a frame of single box-drawing lines (`┌─┐│└┘`, with `┬` and `┴` where the history pane's divider meets the edges) whose top edge carries its title; the text area is not drawn while it is open, and the status line shows the panel's key hints. A panel is modal like the history pane: keys go to it, and it and the history pane are never open together. A paste is dropped by the User Settings panel and the Colors editor, and goes into the search field of the Key Bindings editor or the help's search panel. A prompt opened from a panel (an integer setting's value, the confirmation of a reset of every key) takes the keys until it closes.

**Key bindings.** During `starting`, after `Settings.load`, the `keymap` member of the settings (if any) is handed to [Keymap.apply_overrides](./keymap.hpp.skel.md#function-apply_overrides); the first warning is shown on the status line as "settings.json keymap: <warning>", with "(and N more)". While the Key Bindings editor is open, each [KeymapKeyResult](../ui/keymap_view.hpp.skel.md#symbol-keymapkeyresult) is handled so: `message` goes to the status line; `changed` saves the keymap, that is [Keymap.overrides](./keymap.hpp.skel.md#function-overrides) written with [Settings.set_raw](./settings.hpp.skel.md#function-set_raw) (the member is removed when nothing differs from the defaults; a write failure shows "key bindings not remembered: <reason>" and the bindings still apply for this session); `reset_all` opens a `confirm` prompt (Reset / Cancel) and on Reset calls `KeymapView.reset_all` and saves.

## class: App

- **Inputs:** `options`: a [CliOptions](./cli_options.hpp.skel.md#symbol-clioptions): its `paths` are the files to open, each as a document (none: an untitled buffer); its `settings` are applied with [Settings.override](./settings.hpp.skel.md#function-override) right after the settings load, and its `colors` with [ColorTheme.set_session](../ui/theme.hpp.skel.md#function-set_session) after the saved colors, so neither is ever saved; with `read_only` every document opened from the command line starts in read-only mode (as if View > Read Only were chosen in it); with `persist_history` each of them has Persist History turned on, creating its sidecar, an unreadable sidecar bringing up the same Overwrite/Cancel question as the menu (one file at a time). `terminal`: a `unique_ptr<Terminal>`. A path that cannot be opened (permission, a directory) is skipped and the reason shown on the status line.
- **State changes:** `starting → running → exiting`. During `starting`, [load_configuration](./startup.hpp.skel.md#function-load_configuration) runs before EditorView and Editor are built, so the first frame already uses the remembered settings (the tab width, and the starting values of the View toggles), keys and colors, and the command line's session settings and colors; its warning is shown on the status line. Invariants: at most one of {menu, prompt, history panel, settings panel, key bindings editor} has focus, and a prompt opened over the history panel or a settings panel takes it from them until it closes; the screen is re-rendered before every blocking wait if anything changed.
- **Owns:** see the module. Destruction order: highlighter and LSP shutdown, then the document (sidecar flush, scanner join), then the queue close, then the terminal restore.
- **Access:** main thread only.
- **Referred by:** [app (implementation)](./app.cpp.skel.md)
- **Referred by:** [main](../main.cpp.skel.md)
- **Referred by:** [app_test](../../tests/app_test.cpp.skel.md)
- **Referred by:** [stress_test](../../tests/stress_test.cpp.skel.md)

### function: run

The event loop.

- **Inputs:** none.
- **Returns:** the process exit code.
- **State changes:** each iteration does the following:
  1. Render if dirty.
  2. Compute the timeout as the minimum of the input decoder's pending Esc timeout, the highlighter `tick` deadline, the next 2 s external-change check ([handle_external_change](#function-handle_external_change)), and 0 if a search step is active.
  3. Call `terminal.wait`.
  4. Read and decode input, and dispatch it.
  5. Call `queue.drain`.
  6. Advance any search job by one step.
  7. Handle resize.
  8. If the slow-load prune offer is pending, call [offer_prune](#function-offer_prune).

  Steps 1 and 4–8 (`handle_events`) each run under a guard: an exception is logged and shown as "internal error: …" in the status, and the loop goes on, so the documents can still be saved. `terminal.wait` is outside the guards, so a step that keeps failing still waits for the next event instead of spinning. A `terminated` wake ends the loop in order.
- **Access:** called once by `main`.

### function: run_command

- **Inputs:** a `CommandId`.
- **Returns:** nothing.
- **State changes:** executes the command against the owned components. After every command, `EditorView.scroll_to_cursor` runs.
  - Motions and edits go to Editor. `InsertTab` inserts one tab character (`\t`) as typed text, replacing the selection like any typed character; it never inserts spaces.
  - Save and Save As go to [save_document](#function-save_document). Save on an untitled document opens the `save_as` prompt instead.
  - `Find` opens the Prompt, prefilled with a selection of up to `kMaxFindPrefill` bytes, else the last query ([Prompt.last_query](../ui/prompt.hpp.skel.md#function-open_find); never what another prompt was given). `FindNext` and `FindPrev` (F3, Shift+F3) call [Searcher.find_next](../search/search.hpp.skel.md#function-find_next) from the cursor, forward or backward, with the last query, whether or not the find bar is open. When no query has been set in this session, F3 and Shift+F3 open the find bar exactly as `Find` (Ctrl+F) does. `GotoLine` (Ctrl+G) opens the `goto_line` prompt.
  - `UndoHistory` (Edit > Undo History…; it has no key) opens the [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) with the document's undo tree. While it is open, App lays the screen out as the panel describes. The panel is a side pane on the left of the text area. The text area (with its gutter) stays visible to its right, narrower by the pane's width and the frame. The status line shows the panel's key hints. The pane's width comes from [history_pane_width](../ui/history_view.hpp.skel.md#function-history_pane_width). On a terminal narrower than 60 columns the panel takes the full width while it is open: the text area is not drawn, and it is laid out again when the panel closes. The text area is re-laid out (and `EditorView.scroll_to_cursor` run) when the panel opens and closes. When the panel returns a node, App calls [Document.jump_to](../edit/document.hpp.skel.md#function-jump_to), restores the cursor it returns, clears the selection, and closes the panel; on error it shows the reason and leaves the panel open.
  - `ClearHistory` (C in the Undo History pane; no menu item) on a **clean** document opens a `confirm` prompt: "Delete all undo history for <file>? Deleted text kept in <file>.mod will be gone. Clear / Cancel", with Esc meaning Cancel. Clear calls [Document.clear_history](../edit/document.hpp.skel.md#function-clear_history) and reports "history cleared" or the error. If the [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) is open, it stays open and is opened again on the document's history, so it shows the cleared history dimmed under the new root.
  - `ClearHistory` on a **dirty** document (including an untitled one with content) first opens a `confirm` prompt: "Save changes before clearing history? Save / Cancel", with Esc meaning Cancel. Save calls [save_document](#function-save_document) with the clean-document Clear / Cancel confirmation above as its `then` continuation, so history is cleared only after a successful save and only if the user then confirms Clear. A canceled or failed save (including Cancel in the in-place question or in the Save As prompt for an untitled document) clears nothing.
  - The **Undo History pane's preview** is a [HistoryPreview](./history_preview.hpp.skel.md#class-historypreview) on the focused slot: App calls `begin` when the pane opens (and after any command from the pane, since the history may have changed), `show` after every key that changes the selected node, and `end` before Enter's jump, any command from the pane, and closing it; a preview error becomes the status "cannot preview: <reason>". Tab and Shift+Tab call `toggle_focus`; while the text has the focus, the motions go to `scroll` with a page of the text area's rows less one.
  - `TogglePersistHistory` (P in the Undo History pane) calls [Document.set_persist_history](../edit/document.hpp.skel.md#function-set_persist_history) with the opposite of `persist_history()`. On `format` it asks "Overwrite the unreadable history file <name>.mod?" (Overwrite / Cancel) and calls again with `overwrite`; on `unsupported` the status reads "history is open in another mod"; on success "history saved to <name>.mod" or "history kept in memory only". The pane's checkbox follows.
  - `TrimHistory` (T in the Undo History pane; no menu item; formerly Trim History) runs the **trim flow**, starting at the age prompt:
    1. Calls [Document.prune_preview](../edit/document.hpp.skel.md#function-prune_preview) with no `days`. An error is shown on the status line and ends the flow. If `oldest_days` is none, the status reads "no history to prune" and the flow ends.
    2. Opens the `prune_age` prompt with the oldest age.
    3. On submit, calls `prune_preview(days)`. If `remove_count` is 0, the status reads "nothing older than <days> days" and the flow ends.
    4. Otherwise opens a `confirm` prompt: "Remove <remove_count> of <remove_count + keep_count> changes (older than <days> days, plus <removed_trees> unreachable earlier histories)? Their text will be gone from <file>.mod for good. Prune / Cancel". The parenthesis drops the "plus …" part when `removed_trees` is 0, with Esc meaning Cancel.
    5. Prune calls [Document.prune_history](../edit/document.hpp.skel.md#function-prune_history) with the preview's `cutoff_ms` and reports "history pruned: <remove_count> changes removed" or the error. If the [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) is open, it stays open and is opened again on the document's history.

    The document does not need to be clean: a prune never changes the text.
  - `KeyBindings` closes the history pane and the User Settings panel and opens the Key Bindings editor on the App's Keymap.
  - `UserSettings` closes the history pane and the Key Bindings editor and opens the User Settings panel. A [SettingsKeyResult](../ui/settings_view.hpp.skel.md#symbol-settingskeyresult) with `change` is applied with `apply_setting`; one with `edit` on the `keymap` row runs `KeyBindings`; one with `edit` on an integer opens the `setting` prompt for it, prefilled with its value, which stays open with an inline error until the text is a whole number in the setting's range and then calls `apply_setting`.
  - `RecentSetting1` … `RecentSetting5` act on the setting at that position of [Settings.recent](./settings.hpp.skel.md#function-recent): a boolean is flipped with `apply_setting`, a choice steps to its next name (wrapping), an integer opens the same `setting` prompt. A position past the end of the list does nothing.
  - View toggles flip the focused document's [ViewOptions](./document_slot.hpp.skel.md#class-viewoptions) and apply them to every view of it; Syntax Coloring also swaps the document's highlighter. `ToggleWordWrap` sets each view's word wrap with [EditorView.set_word_wrap](../ui/editor_view.hpp.skel.md#function-set_word_wrap). `ToggleReadOnly` turns read-only on or off in every view of the document; a view showing a followed link target comes back to the document when it is turned off (the focused view, while away, keeps its existing behavior of opening the target for editing).
  - `Exit` (Ctrl+Q, File > Exit) begins the dirty-check flow. Esc never exits.
  - `Suspend` (Ctrl+T, File > Suspend) calls [Terminal.suspend](../platform/terminal.hpp.skel.md#function-suspend); when the job continues, the screen is invalidated and redrawn at the terminal's size. Nothing is saved or asked first, as with any stopped shell job.
  - `CutToLineEnd` (Ctrl+K) calls [Editor.cut_to_line_end](../edit/editor.hpp.skel.md#function-cut_to_line_end) with `append` true when the input event just before was also `CutToLineEnd`. App keeps that as a flag that every dispatched event clears and only this command sets, so typing, a paste, a cursor move or any other command between two presses starts a new clipboard.
  - `ShowMenu` shows the hidden [MenuBar](../ui/menu.hpp.skel.md#class-menubar), or hides it when it is visible; while the bar is visible every key goes to it, except `ShowMenu`'s own key, which hides it. `OpenMenu…` open one menu directly.
- **Access:** input dispatch and the menu.
- **Depends on:** [Document.line_start](../edit/document.hpp.skel.md#function-line_start)

### function: offer_prune

The slow-load offer. The user's rule: if the sidecar takes more than 5 seconds to load, ask whether to prune it, and how old to prune.

- **Inputs:** none. Ambient: [Document.history_load_ms](../edit/document.hpp.skel.md#function-history_load_ms) and the history state.
- **Returns:** nothing.
- **State changes:**
  - [open_document](#function-open_document) sets the flag `prune_offer_pending` when `history_load_ms() > 5000`, and clears it otherwise, so a later File > Open of a fast file cancels an offer that had not been made yet. `run` calls `offer_prune` while the flag is set and no menu, prompt or history panel is open.
  - While the history state is `verifying`, or the root's base hash is pending, it does nothing and the flag stays set. The question therefore appears once verification resolves, which on a large file can be well after the load itself.
  - In any other state it clears the flag. If [Document.prune_preview](../edit/document.hpp.skel.md#function-prune_preview) with no `days` fails or has no `oldest_days`, it shows nothing.
  - Otherwise it opens a `confirm` prompt: "History took <load_ms / 1000, one decimal> s to load. Prune old history? Prune… / Not now", with Esc meaning Not now. Prune… continues with step 2 of the prune flow in `run_command`.
  - Not now, and Cancel later in the flow, are **not remembered**: the next load of any document that takes more than 5 seconds asks again.
- **Access:** `run`.
- **Depends on:** [Document.prune_preview](../edit/document.hpp.skel.md#function-prune_preview)
- **Depends on:** [Document.prune_history](../edit/document.hpp.skel.md#function-prune_history)

### function: save_document

The one save flow, used by File > Save, File > Save As, Ctrl+S, and the save branch of the dirty checks on exit and open.

- **Inputs:** `target`: none for Save, or the path entered in the `save_as` prompt; `then`: an optional continuation run only after a successful save (for example, exit).
- **Returns:** nothing. The outcome arrives through prompts and the status line.
- **State changes:**
  0. On an untitled document with no `target`, opens the `save_as` prompt first and continues with the entered path as `target`. Esc in that prompt cancels the save and runs no continuation. This is how the exit and open dirty checks save an untitled buffer.
  1. Calls [handle_external_change](#function-handle_external_change) first. If an unanswered external change is pending, the save waits for that prompt: Reload cancels the save; Keep continues it, and the save then overwrites the other program's change. After a deletion there is no prompt, and the save recreates the file.
  2. Calls [Document.save](../edit/document.hpp.skel.md#function-save) or [Document.save_as](../edit/document.hpp.skel.md#function-save_as) with `mode = atomic`.
  3. On `not_atomic`, opens a `confirm` prompt: "Cannot save safely: <reason>. Write in place (not crash-safe) / Cancel". Write in place repeats the call with `mode = in_place`, passing App's [Clipboard](../edit/clipboard.hpp.skel.md#class-clipboard) so that the document can materialize clipboard content that points into the overwritten file; Cancel leaves the document dirty and runs no continuation. The question is asked on every such save, never remembered. If `target` does not exist yet, in-place writing is impossible, so the error is reported without the prompt.
  4. On success, runs `then`, and after a Save As re-picks the highlighter as `open_document` does.
- **Access:** main thread, from `run_command` and the dirty-check prompts.
- **Depends on:** [Document.save](../edit/document.hpp.skel.md#function-save)
- **Depends on:** [Document.save_as](../edit/document.hpp.skel.md#function-save_as)

### function: handle_external_change

Asks the user what to do when the file changed on disk.

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** calls [Document.check_external_change](../edit/document.hpp.skel.md#function-check_external_change), then:
  - `modified` with `replaced` true (the other program wrote a new file over this one): opens a `confirm` prompt: "<file> changed on disk. Reload (starts a new history; unsaved changes leave the undo path) / Keep my version". Reload calls [Document.reload](../edit/document.hpp.skel.md#function-reload), then rebuilds the highlighter (the LSP client receives a fresh `didOpen`). Keep calls [Document.keep_in_memory](../edit/document.hpp.skel.md#function-keep_in_memory). The prompt has no Cancel: Esc means Keep, so the question is never silently re-asked for the same change.
  - `modified` with `replaced` false (the other program wrote into the same file, so the in-memory version no longer exists): opens a `confirm` prompt with a single choice: "<file> was modified in place on disk; your version cannot be kept. Reload". Reload proceeds as above. Esc also means Reload, because there is nothing else to do; the prompt cannot be dismissed without reloading.
  - `deleted`: no prompt. Shows "<file> deleted on disk; saving will recreate it" on the status line and calls [Document.keep_in_memory](../edit/document.hpp.skel.md#function-keep_in_memory).
- **Access:** `run`, every 2 s while no menu or prompt is open (a check that falls due while one is open is deferred until it closes), and `save_document`.
- **Depends on:** [Document.check_external_change](../edit/document.hpp.skel.md#function-check_external_change)
- **Depends on:** [Document.reload](../edit/document.hpp.skel.md#function-reload)
- **Depends on:** [Document.keep_in_memory](../edit/document.hpp.skel.md#function-keep_in_memory)
- **Depends on:** [ColorTheme](../ui/theme.hpp.skel.md#class-colortheme)
- **Depends on:** [ColorsView](../ui/colors_view.hpp.skel.md#class-colorsview)
- **Depends on:** [SyntaxHighlighter](../syntax/syntax_highlighter.hpp.skel.md#class-syntaxhighlighter)
- **Depends on:** [LayeredHighlighter](../syntax/layered_highlighter.hpp.skel.md#class-layeredhighlighter)
- **Depends on:** [TerminalOutput](../platform/terminal_output.hpp.skel.md#class-terminaloutput)
- **Depends on:** [HelpViewer](../ui/help_viewer.hpp.skel.md#class-helpviewer)
- **Depends on:** [ConfirmBar](../ui/confirm_bar.hpp.skel.md#class-confirmbar)
- **Depends on:** [FileDialog](../ui/file_dialog.hpp.skel.md#class-filedialog)
- **Depends on:** [split_rows](./split_layout.hpp.skel.md#function-split_rows)
- **Depends on:** [max_splits](./split_layout.hpp.skel.md#function-max_splits)
- **Depends on:** [focus_after_unsplit](./split_layout.hpp.skel.md#function-focus_after_unsplit)
- **Depends on:** [ReadingLayout](../ui/reading_layout.hpp.skel.md#class-readinglayout)
- **Depends on:** [CliOptions](./cli_options.hpp.skel.md#symbol-clioptions)

### function: open_document

- **Inputs:** `path`, or none at startup without arguments, which creates the document with [Document.open_untitled](../edit/document.hpp.skel.md#function-open_untitled) and picks no highlighter.
- **Returns:** `Status`.
- **State changes:** opens a new [Document](../edit/document.hpp.skel.md#function-open), rebuilds the Editor, EditorView and Highlighter, and picks the highlighter: Markdown by extension (when Syntax Coloring is on), else LSP if [LanguageConfig.find_for_path](../syntax/language_config.hpp.skel.md#function-find_for_path) matches and the size is under the cap; when it matches but the file is over the cap, no highlighter is created and the status reads "LSP off: file too large". The new highlighter is registered with `Document.add_listener` (and the old one removed before it is destroyed), because highlighters only hold a `const Document&`. On failure, the current document is kept.
- **Access:** startup and File > Open, after the dirty check.
