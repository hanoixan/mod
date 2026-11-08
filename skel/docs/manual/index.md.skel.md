---
role: product
stamp: source 1b1117f6, stand-in 8f286d25
---
# resource: index.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own view mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: The manual's root, shown first by F1 and read by anyone opening the manual. A short description of mod, then one line per page with a relative link to it, and a link to [sidecar-format.md](../sidecar/sidecar-format.md.skel.md). Every other page must be reachable from here; also a line linking the Colors page; also the command-line page.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
