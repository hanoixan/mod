---
role: product
stamp: source 68118240, stand-in 8608426f
---
# resource: editing.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: Moving, selecting, word and line motions; typing, Enter keeping the file's line ending, Tab and its width; the clipboard: copy, cut, paste, Cut to Line End (Ctrl+K) and its appending, OSC 52, pasted line breaks; undo and branches in one paragraph; Suspend (Ctrl+T) and `fg`, and on Windows only from a shell that can continue mod (MSYS2's bash), else mod says so.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
