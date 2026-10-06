---
role: test
stamp: source e7a3d4b7, stand-in e26830d3
---
# module: folder_tree_view_test

Rows drawn with ▾/▸ and indents, the short hints on the panel's own line in a narrow panel, a hidden name dim, the selection highlighted only while focused; keys: arrows, pages and the ends move, Right and Left open and close, Enter opens a folder or a file, Space previews a file and does nothing on a folder, Shift+Right is back and Esc leave; the selected row kept in view and a long name panned into view whole, back to the left edge for a short one; the full hints when they fit; the tree's message in place of the hints. The selected row is listSelected with the keys and listSelectedUnfocused without them.

- **Owns:** its scratch folders.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [FolderTreeView](../src/ui/folder_tree_view.hpp.skel.md#class-foldertreeview)
- **Depends on:** [fs_probe](./fs_probe.hpp.skel.md)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
