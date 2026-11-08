---
role: product
stamp: source 5782bd7f, stand-in 9e3fbc4c
---
# resource: view-mode.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own view mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: Turning view mode on and off (View > View Mode, `-v`), what is refused, the view layout by file type (Markdown by extension or a heading on an extensionless file's first line; any other file its text, read-only), moving and copying, Tab / Shift+Tab, Enter / Space on each kind of link, Ctrl+Left / Ctrl+Right, how a followed file is shown and listed, and turning it off on a followed file; also Markdown laid out for reading in view mode without line numbers, nested quotes, wide tables fitted and wrapped and long code lines wrapped with Word Wrap on (panned with it off, or with too many columns), and the read_only_copy setting.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
