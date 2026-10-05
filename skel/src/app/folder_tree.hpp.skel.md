---
role: product
stamp: source 74cbc0af, stand-in d3c68920
---
# module: folder_tree

The folder tree's contents, kept apart from its drawing and from App so it can be tested on real folders. The root is the folder mod started in.

- **Owns:** the rows shown, the folders open this session, the selection, the last read error.
- **Access:** App, [FolderTreeView](../ui/folder_tree_view.hpp.skel.md#class-foldertreeview). Main thread.
- **Required:** always.
- **Failure modes:** a folder that cannot be read (permissions, gone): opening it fails, it stays closed, and `message()` says why; an entry whose type cannot be read is listed as a file.
- **Depends on:** none
- **Unknowns:** none

## symbol: TreeRow

One shown row: `path`, `name` (the root's is its folder name), `depth` (0 for the root), `dir`, `expanded`, `hidden` (the name starts with `.`).

- **Access:** FolderTreeView, tests.

## class: FolderTree

- **Inputs:** `root`: the folder shown.
- **State changes:** the rows are rebuilt whenever a folder opens or closes: the root (always open), then the entries of each open folder after it, one level deeper, folders first and then files, each by name compared without regard to case (then as written). Every entry is listed, hidden ones and `.git` included. Each open folder is read again on every rebuild, so a folder shows what it holds when it was last opened. The selection keeps its path across a rebuild when that row is still shown, else goes to the root.
- **Owns:** as the module.
- **Access:** App, FolderTreeView.
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [folder_tree (implementation)](./folder_tree.cpp.skel.md)
- **Referred by:** [folder_tree_view](../ui/folder_tree_view.hpp.skel.md)
- **Referred by:** [folder_tree_test](../../tests/folder_tree_test.cpp.skel.md)

### function: move

- **Inputs:** `delta`: rows down (negative: up).
- **Returns:** nothing.
- **State changes:** the selection moves, never past either end. `select(i)` selects row `i` (clamped).
- **Access:** FolderTreeView.

### function: expand

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** Right: the selected folder, if closed, opens and is read now; if it cannot be read, `message()` says why ("cannot open <name>: <reason>") and it stays closed. Nothing on a file.
- **Access:** FolderTreeView.

### function: collapse

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** Left: the selected folder, if open and not the root, closes; otherwise the folder holding the selected row is selected. `toggle()` (Enter on a folder) opens a closed folder and closes an open one.
- **Access:** FolderTreeView.
