---
role: product
stamp: source 452ea11b, stand-in 58ad566b
---
# resource: menus.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: The hidden bar at the top, shown with Esc (or F10, Alt+X) and then a menu's letter (Esc F X exits), menus opening downward, the flash, moving in it, when it hides, Esc three times to quit, the macOS Option-as-Meta note, and one section per menu (File, Edit, View, Documents, Options, Help) listing its items in the order MenuBar builds them, with links to the pages that explain them; also Options > Colors… and View > Syntax Coloring; also View > Split and Unsplit.

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** [MenuBar](../../src/ui/menu.hpp.skel.md#class-menubar)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
