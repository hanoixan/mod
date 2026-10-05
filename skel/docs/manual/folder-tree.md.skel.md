---
role: product
stamp: source 0fbf40b8, stand-in 8a73727e
---
# resource: folder-tree.md

A page of the user manual, `docs/manual/`, in plain Markdown with relative links only, so it reads the same in any viewer, on a code host, and in mod's own read-only mode and F1 help. Written for people using mod, in short plain sentences; it states what mod does, never how the code does it. Installed to `<prefix>/share/mod/doc/` by [CMakeLists.txt](../../CMakeLists.txt.skel.md).

Contents: what the panel lists (every entry, folders first, by name, hidden names dimmed), what hides it, that it shows only while you are in it unless pinned, View > Pin Folder Tree, the pin_folder_tree setting and its option; moving into it (Esc, Shift+Left) and what the view and the tree's own line show then; its keys (moving, panning a long name, Right and Left, Enter on a folder or a file, Space's read-only preview and Esc out of it, Shift+Right back to escape mode, Esc back to the view, other commands leaving it).

- **Required:** optional — the editor works without its manual.
- **Failure modes:** the page drifts from the code; [docs_test](../../tests/docs_test.cpp.skel.md) catches broken links and anchors, and checks the facts the code can confirm (every setting, every default key, the defaults file).
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
