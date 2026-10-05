---
role: product
stamp: source 15c07b20, stand-in dd0acbe2
---
# resource: read-only.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: Turning read-only mode on and off, what is refused, moving and copying, Tab / Shift+Tab, Enter / Space on each kind of link, Ctrl+Left / Ctrl+Right, how a followed file is shown and listed, and turning it off on a followed file; also Markdown laid out for reading in read-only mode, and the read_only_copy setting.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
