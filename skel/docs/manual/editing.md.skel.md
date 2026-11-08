---
role: product
stamp: source 15568bdc, stand-in f0b73a15
---
# resource: editing.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: Moving, selecting, word and line motions; Home and End on a wrapped line (the row, twice quickly the line; the Visual commands in Key bindings); Ctrl+Up and Ctrl+Down scrolling the view past the cursor and the view coming back to it; the mouse (the `mouse` setting; click, drag, double and triple click, Shift+click, the wheel, the menus, dialog and tree; mod's own selection, Shift+drag for the terminal's, OSC 52 caveat for GNOME Terminal and Konsole); typing, Enter keeping the file's line ending, Tab and its width; the clipboard: copy, cut, paste, Cut to Line End (Ctrl+K) and its appending, OSC 52, pasted line breaks; undo and branches in one paragraph; Suspend (Ctrl+T) and `fg`, and on Windows only from a shell that can continue mod (MSYS2's bash), else mod says so.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
