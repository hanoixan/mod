---
role: test
stamp: source d0f06bd1, stand-in 366134c3
---
# module: app_test

The real App on a [ScriptedTerminal](./scripted_terminal.hpp.skel.md#class-scriptedterminal), with files, read-only mode and the TERM value set per case: Find opens with the last search, not with what another prompt (Go to Line) was given; the focused view's status line starts with `>`, with one view too, and with two splits the mark follows the focus, moved in escape mode by Shift+Down and Shift+Up and no longer by PageUp; the terminal's title names the focused view's document, with `*` while unsaved, follows Save, a focus move and a document shown from the Documents menu, and is never sent twice in a row the same; following a link in read-only mode keeps the view's own document's title; a VT100 is never sent a title. The bottom band: with one view a prompt takes the bottom line and pushes the status line up; with two splits a prompt takes the lower split's bottom row (the upper unchanged) and dims the other split's text until it closes, and the menu bar is the top line, the upper split giving up its first row, and dims the other split too; a question takes the bottom lines (rule, question, buttons) with the status line above; a full-screen UI (the file dialog) has its own bottom line, with no document's name or position anywhere. Three Escapes about 300 ms apart do not quit; three each within 150 ms of the last do. The folder tree (the test's own working folder as root): Esc, Shift+Left shows it and gives it the keys (no menu bar, the view's status line without `>`), Enter on a file opens it in the view and gives the keys back (the unpinned tree going); Space previews a Markdown file laid out, alone, Esc closes the preview, Shift+Right goes back to escape mode, Esc back to editing; View > Pin Folder Tree keeps it shown or hides it, and the pin_folder_tree setting pins it at start; unpinned, Shift+Right, Esc and Enter on a file each leave it gone, while pinned it stays after Shift+Right and Esc; Ctrl+Q from the tree quits. F10, Alt+X, and a mix of Esc, F10 and Alt+X, three times quickly, quit like Esc; another key between them starts the count again.

- **Owns:** test fixtures only (a file and a config folder under the scratch directory).
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [App](../src/app/app.hpp.skel.md#class-app)
- **Depends on:** [ScriptedTerminal](./scripted_terminal.hpp.skel.md#class-scriptedterminal)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
