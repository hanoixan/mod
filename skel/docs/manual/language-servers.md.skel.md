---
role: product
stamp: source 88ca46ae, stand-in c3821ab4
---
# resource: language-servers.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: syntax coloring in two layers (the built-in syntax layer for keywords, strings, comments and numbers, from data; a language server's tokens for names, drawn over it), which languages have built-in syntax descriptions and which have servers, `LSP off` and the toggle, the user `languages.json` with every entry field including `fileNames` and the `syntax` object (with an example adding a language), sharing servers by project root and the markers, restarts, and how to install a server (basedpyright with `uv`, and why plain pyright gives no colors: it sends no semantic tokens).

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
