---
role: product
stamp: source 3eaeb111, stand-in 5d592502
---
# resource: documents.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own view mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: Opening several documents (command line, File > Open, already open, replacing an empty untitled buffer), the Documents menu, Close, quitting with unsaved documents, what each document keeps, what is shared, and shared language servers; also split views (Split, Shift+Up/Shift+Down after Esc to move, Unsplit, shared documents, single-view mode, the H/3 limit, the focused view's `>` (with one view too) and the others on a darker band; prompts and questions on the screen's bottom lines below every view and the menu bar on its top line, the bottom view (or the top one) giving up rows and the others dimming; a full-screen UI's own bottom line); and the terminal's title (`mod:<name>`, ` *` while unsaved, the view's own document, restored on exit and suspend, none on a VT100); changes on disk: every document checked, auto_reload, the notices on the status line (never a question), File > Reload, deletion, a file rewritten in place under a mapping, and on Windows the file read in and closed so others can save it; with unsaved changes the question Load from disk / Ignore / Merge in place (Ignore on Enter and Esc), the diff3 conflict blocks (yours, base, theirs, as git writes them) and their coloring with `auto_color_merges` (marker lines inverted, each side's additions and the base text it deleted in that side's merge colors, text both added or deleted in their own, any diff3 block, plain again once a marker goes), one undo step, the background merge with its progress bar and Esc, the same question on saving, the conflicts question on saving and the notice on opening.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
