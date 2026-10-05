---
role: product
stamp: source 6221a102, stand-in d3f78ecf
---
# module: folder_tree_view

The folder tree's panel: draws a [FolderTree](../app/folder_tree.hpp.skel.md#class-foldertree) and turns keys into what App should do.

- **Owns:** the first row shown, the sideways pan, a page's height, App's message.
- **Access:** App. Main thread.
- **Required:** always.
- **Failure modes:** none.
- **Depends on:** [FolderTree](../app/folder_tree.hpp.skel.md#class-foldertree)
- **Depends on:** [text_columns](../text/utf8.hpp.skel.md#function-text_columns)
- **Unknowns:** none

## symbol: TreeKeyResult

What a key asks App to do: `kind` of `enum class TreeKey { none, moved, preview, open, back, leave }`, with the file's `path` for `preview` and `open`.

- **Access:** App.

## class: FolderTreeView

- **Inputs:** the tree, through `set_tree`.
- **State changes:** as its functions.
- **Owns:** as the module.
- **Access:** App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [folder_tree_view (implementation)](./folder_tree_view.cpp.skel.md)
- **Referred by:** [folder_tree_view_test](../../tests/folder_tree_view_test.cpp.skel.md)

### function: handle_key

- **Inputs:** a key.
- **Returns:** Shift+Right: `back`; Esc: `leave`; Up, Down, PageUp, PageDown (a page is the rows shown, less the panel's own line), Home, End: the selection moves (`moved`); Right and Left: the tree's `expand` and `collapse`; Enter: on a file `open` with its path, on a folder the tree's `toggle`; Space on a file: `preview` with its path; anything else (Space on a folder, other modified keys) `none`. Any key clears App's message.
- **State changes:** the tree's selection and open folders.
- **Access:** App.

### function: render

- **Inputs:** `screen`, `area`, `focused`.
- **Returns:** nothing.
- **State changes:** draws the rows above the area's last row: two columns of indent a level, `▾ ` before an open folder and `▸ ` before a closed one (two spaces before a file), the name; hidden names dim; the selected row in `listSelected` while `focused`, in `listSelectedUnfocused` otherwise (the same look as the Undo History pane's). The selected row is kept in view (the first row shown moves the least), and the rows are panned sideways so the selected row's whole text shows, back to the left edge whenever it fits. The last row is the panel's own line in the status color: App's message (`set_message`, until the next key), else the tree's message, else the hints `Space: preview  Enter: open  Shift+Right: back`, or `Space: view  Enter: open` when those do not fit.
- **Access:** App.
