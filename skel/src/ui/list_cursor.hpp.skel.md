---
role: product
stamp: source 2a486877, stand-in 548102b2
---
# module: list_cursor

The selection and scrolling arithmetic the panels' lists share (User Settings, Key Bindings, Colors, Search Help), so each moves and scrolls its list the same way.

- **Owns:** nothing.
- **Access:** the panel views.
- **Required:** always.
- **Failure modes:** none.
- **Depends on:** [Key](./input.hpp.skel.md#symbol-key)
- **Unknowns:** none

## symbol: kListPage

The rows Page Up and Page Down move: 10.

- **Access:** the panel views, through list_step.

## symbol: ListStep

Where a list key takes the selection (`index`) and which way to look from there (`direction`, -1 up or 1 down) for a row that can be selected, for a list whose header rows cannot (Colors).

- **Access:** the panel views.

## function: list_step

- **Inputs:** `key`; `selected`: the selected row; `count`: the rows of the list.
- **Returns:** for Up and Down one row, Page Up and Page Down `kListPage` rows, Home and End the ends, never past either end, with the direction of the move (Up, Page Up and End look up; Down, Page Down and Home look down); nullopt for any other key. An empty list lands on row 0, which the caller checks before using it.
- **State changes:** none.
- **Access:** the panel views.
- **Referred by:** [colors_view (implementation)](./colors_view.cpp.skel.md)
- **Referred by:** [doc_search_view (implementation)](./doc_search_view.cpp.skel.md)
- **Referred by:** [keymap_view (implementation)](./keymap_view.cpp.skel.md)
- **Referred by:** [list_cursor (implementation)](./list_cursor.cpp.skel.md)
- **Referred by:** [settings_view (implementation)](./settings_view.cpp.skel.md)
- **Referred by:** [list_cursor_test](../../tests/list_cursor_test.cpp.skel.md)

## function: scroll_to_show

- **Inputs:** `selected`; `scroll`: the first row shown; `visible`: the rows shown (at least 1 is used).
- **Returns:** the first row to show so the window shows `selected`, moved from `scroll` only as far as it must: unchanged when it is already shown, the selection as the first row when it is above, as the last row when it is below.
- **State changes:** none.
- **Access:** the panel views.
- **Referred by:** [colors_view (implementation)](./colors_view.cpp.skel.md)
- **Referred by:** [doc_search_view (implementation)](./doc_search_view.cpp.skel.md)
- **Referred by:** [keymap_view (implementation)](./keymap_view.cpp.skel.md)
- **Referred by:** [settings_view (implementation)](./settings_view.cpp.skel.md)
- **Referred by:** [list_cursor_test](../../tests/list_cursor_test.cpp.skel.md)
