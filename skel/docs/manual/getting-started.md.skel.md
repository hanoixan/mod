---
role: product
stamp: source 7852b2bf, stand-in 7ecba343
---
# resource: getting-started.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: Starting mod with no file, one or several files, `--help` and `--version`; a path that does not exist yet; the screen (menu bar, text with line numbers and the overflow `>`, status line); saving, Save As, safe saving and the in-place question; quitting with unsaved documents; the `.history` history file next to a file whose Persist History is on; where settings live; also -ro and a pointer to the command-line page.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
