---
role: product
stamp: source 25f1f338, stand-in 105563a8
---
# resource: search.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: The find bar: incremental search, Enter / Shift+Enter, F3 / Shift+F3, Alt+R / Alt+C / Alt+W; files of any size; Replace (Tab, Enter, Alt+A as one undo step, `$1` groups in regex mode); find only in read-only mode; Go to Line.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
