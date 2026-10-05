---
role: product
stamp: source 8a908a72, stand-in 48a3296e
---
# resource: key-bindings.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: How the Key Bindings editor works (search, add, refuse a taken key, remove, reset, `*`), that only changes are stored, what cannot be bound, then tables of **every** default binding, one row per key with the key label in backticks and the command's display name, grouped by purpose; then the terminal notes.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** [Keymap.default_bindings](../../src/app/keymap.hpp.skel.md#function-default_bindings)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
