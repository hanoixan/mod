---
role: product
stamp: source ce3c3f05, stand-in 8d6c00ef
---
# module: split_layout

The arithmetic of split views, kept pure so it can be tested on its own: how many splits a screen may hold, how the rows are shared, and which split is focused after one is removed.

- **Owns:** nothing.
- **Access:** public. Pure functions.
- **Required:** always.
- **Failure modes:** none.
- **Depends on:** none
- **Unknowns:** none

## function: max_splits

- **Inputs:** `rows`: the screen's height.
- **Returns:** `rows / 3`, at least 1: each split keeps a text row, a row for the menu bar when it is shown over it, and its status line.
- **State changes:** none.
- **Access:** App.
- **Referred by:** [App.handle_external_change](./app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [split_layout_test](../../tests/split_layout_test.cpp.skel.md)

## function: split_rows

- **Inputs:** `rows`: the rows to share; `count`: the splits, at least 1.
- **Returns:** each split's `{ top, height }`, top to bottom, the heights as even as they can be: `rows / count` each, the remainder one row each to the top splits.
- **State changes:** none.
- **Access:** App's layout.
- **Referred by:** [App.handle_external_change](./app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [split_layout (implementation)](./split_layout.cpp.skel.md)
- **Referred by:** [split_layout_test](../../tests/split_layout_test.cpp.skel.md)

## function: focus_after_unsplit

- **Inputs:** `focus`: the index of the split removed (there were at least two).
- **Returns:** the index, after the removal, of the split to focus: the one above (`focus - 1`), or when there is none the one that was below (now index 0).
- **State changes:** none.
- **Access:** App.
- **Referred by:** [App.handle_external_change](./app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [split_layout_test](../../tests/split_layout_test.cpp.skel.md)
- **Referred by:** [workspace](./workspace.hpp.skel.md)
