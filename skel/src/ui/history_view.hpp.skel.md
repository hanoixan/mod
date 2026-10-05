---
role: product
stamp: source af5a27a3, stand-in 014701b2
---
# module: history_view

The undo-history panel. The user asked for "a tree-view panel so the user can navigate the entire branched undo history and continue from a chosen point". It shows the [UndoTree](../edit/undo_tree.hpp.skel.md#class-undotree) as a tree, lets the user move a selection through it, and on Enter makes the selected node the document's current state through [Document.jump_to](../edit/document.hpp.skel.md#function-jump_to). The next edit then branches from that node, so "continuing from a chosen point" never discards anything: the path the user left stays in the tree as a sibling branch.

It is opened by the `UndoHistory` command, from Edit > Undo History… only; it has no key. While it is open it has the input focus, like the menu and the prompt, so the document cannot be edited underneath it.

Moving the selection does not change the document, but the text beside the pane **previews** the selected state: App shows it with [Document.begin_preview](../edit/document.hpp.skel.md#function-begin_preview), at the same lines that were in view when the pane opened, the step's inserted text highlighted and its removed text struck through. Only Enter changes the document, jumping to the selected state. Esc closes the panel and leaves the document exactly as it was. **Tab** and **Shift+Tab** move the focus between the pane and the previewed text; with the text focused, Up, Down, PageUp, PageDown, Home and End scroll it, read-only, and Enter and Esc work as in the pane. `selected_node()` gives the node of the selected row (none for a read-only one, which previews the current state).

The pane also holds what manages the history as a whole, in a footer under the list: **Clear History…** (C), **Trim History…** (T) and the **Persist History** checkbox (P), which says whether the history is written to the `.history` file beside the document (see [Document.set_persist_history](../edit/document.hpp.skel.md#function-set_persist_history)); it is off by default. They are not in any menu. Each asks its questions in a prompt over the pane, and the pane stays open and shows the history as it is afterwards.

#### Layout

The panel is a **side pane on the left** of the text area. The text stays visible to its right, narrower, with its gutter. The whole is framed, and the status line shows the panel's key hints:

```text
File Edit View Options Help
┌────────────────┬─────────────────────┐
│ ○ 11 delete    │  1 # Notes          │
│ ○ 12 typed     │  2                  │
│ ├─○ 13 paste   │  3 First line       │
│ ● 14 typed     │  4 Second line      │
│                │  5                  │
│────────────────│  6                  │
│ Clear History… │  7                  │
│ Trim History…  │  8                  │
│ [x] Persist Hi…│  9                  │
└────────────────┴─────────────────────┘
 Enter: jump   C: clear   T: trim   P: persist   Esc: close
```

- The frame starts at the top of the body (row 0, or row 1 while the menu bar shows on row 0). The frame's top and bottom border rows and its three vertical rules take two rows and three columns from the text area. The last row is the status line, which shows ` Enter: jump   C: clear   T: trim   P: persist   Esc: close` while the panel is open, replaced temporarily by any status message (such as a refused Enter).
- The bottom four rows of the pane are the **footer**: a rule of `─`, then `Clear History…`, `Trim History…` and `[x] Persist History` (`[ ]` when off), one per row, in the `menu` style with the key letter underlined (C, T, P), as a menu shows its accelerators. The checkbox's state is given with `set_persist`. The node list takes the rows above it and scrolls within them. A pane under six rows tall has no footer, so the list always keeps at least two rows; the keys still work.
- Rows are drawn **oldest first**, so the latest changes are at the bottom, and the tree reads like the folder tree. A **line** runs down from a change through its **newest child** (the highest `NodeId`), to a leaf. A change's other, older children start **side branches**: each is its own line, one level deeper, listed under the change it split from (oldest first) before that change's line goes on. Each row is the branch connectors, then `○` (`●` for the current state), then `>` while the change has side branches that are closed, then a space, the node's `NodeId` and its `EditKind` label. Each level is two columns: a branch's first change has `├─`, its later changes `│ `, and every enclosing branch adds a `│ ` before them. Labels are the `EditKind` names, except `typing`, which shows as `typed`; so `typed`, `delete`, `paste`, `cut`, `replace`, `replace_all`, `newline`, `indent` and `other`.
- Branches **open and close like folders**: every change with side branches is opened (Right) and closed (Left) on its own; opening one shows its side branches with their own side branches closed. When the pane opens, everything is closed except the fewest branches that show the current state: every change the current state's path branched off from.
- Indentation grows with branch nesting, never with node depth: a linear history of a million edits is a single line, one column of `○`.
- After the kind, if the row has room, come a save-point marker (` saved`) and the node's time relative to now, two spaces after the rest (`just now` under a minute, then `N min ago`, `N h ago`, `N days ago`); the row is truncated on the right to the pane width. With 13 and 14 both children of 12 and 14 current, the pane opens on `○ 11 delete`, `○> 12 typed`, `● 14 typed`; Right on 12 gives `○ 12 typed`, `├─○ 13 paste`, `● 14 typed`.
- In vt100 terminal mode the [screen](./screen.hpp.skel.md#class-screen) sends the connectors through the VT100's line-drawing set and `○` and `●` as `o` and `*` (see [TerminalOutput.append_cell](../platform/terminal_output.hpp.skel.md#function-append_cell)).

#### Other histories

The panel shows the **whole forest**: the other trees at the top, oldest (by newest `NodeId`) first, each **closed** to one row, its newest line's last change with `>`, which Right opens to the whole tree (its own branches closed) and Left, on any row of its outer line, closes again; the current state's tree at the bottom, always open. Other trees are histories recorded before a reload, before a verify mismatch or session-only fallback, and (for the rest of the session only) before a Clear History. History removed by a prune ([Document.prune_history](../edit/document.hpp.skel.md#function-prune_history)) is **not** shown at all: it is gone, not dimmed, and the pruned tree's root is drawn like any other root; see [UndoTree.reset](../edit/undo_tree.hpp.skel.md#function-reset). Their rows are **dimmed and read-only**: the selection can move onto them to browse, but Enter on one refuses with "read-only: history from before a reload or Clear History", and the panel stays open. Their base content is gone, so they can never become current.

- **Owns:** the open or closed state, the flattened row list, the set of open changes and open older trees, the selected row, and the panel's scroll offset.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** optional — without it, branches are still reachable with undo, redo and Edit > Next/Previous Branch, one step at a time.
- **Failure modes:**
  - A history of millions of nodes. Node metadata is already in memory (about 100 bytes per node), and the flattened row list costs a few bytes per row, built in O(nodes) when the panel opens. Rendering touches only the visible rows. No payload is read to draw the panel.
  - The selected node is in another root's tree, or is retired by Clear History. Its base content is not available, so Enter refuses with a status message and the panel stays open.
  - Many branches open at once: the graph can be wider than the pane. Rows are truncated on the right, so the id and kind of a deeply indented row can be hidden; accepted.
  - The selected node predates verification while the history state is `verifying`: Enter refuses with the same message that undo gives, and the panel stays open.
  - A long jump: `jump_to` applies one undo or redo step per edge on the path, synchronously. A path of hundreds of thousands of nodes blocks the UI for as long as that takes. Each step is O(ops × log pieces) and never copies payload bytes, because `SidecarRef` and `Pieces` payloads are inserted as pieces.
  - The terminal is too narrow to show the panel and the text side by side (under 60 columns): the panel takes the full width while it is open, and the text is hidden until it closes.
- **Depends on:** [UndoTree.node_info](../edit/undo_tree.hpp.skel.md#function-node_info)
- **Depends on:** [Document.jump_to](../edit/document.hpp.skel.md#function-jump_to)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Depends on:** [KeyEvent](./input.hpp.skel.md#symbol-keyevent)

- **Unknowns:** none. The pane's width was decided: one third of the terminal width, never under 20 or over 40 columns, and the full width on terminals narrower than 60 columns; see [history_pane_width](#function-history_pane_width). The sketch above is narrower than that, for illustration only.

## function: history_pane_width

- **Inputs:** `columns`: the terminal width.
- **Returns:** a `PaneWidth { int width; bool full_width; }`: the width of the pane's row area, inside the frame, in columns, together with `full_width`:
  - When `columns < 60`: `columns − 2` (the frame's left and right borders), and `full_width` true. The text area is not drawn while the panel is open.
  - Otherwise: `clamp(floor(columns / 3), 20, 40)`, and `full_width` false. The text area gets `columns − width − 3`, where the 3 columns are the frame's three vertical rules. That is at least 37 at 60 columns.
  - Examples: 59 → 57 (full width); 60 → 20; 80 → 26; 120 → 40; 200 → 40.
- **State changes:** none. It is pure.
- **Access:** public. [App](../app/app.hpp.skel.md#class-app) uses it for the layout, and `render` for truncation; tests check it.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [history_view_test](../../tests/history_view_test.cpp.skel.md)

## symbol: HistoryRow

`{ NodeId node; std::string graph; int depth; bool expandable; bool expanded; std::optional<NodeId> branch_from; bool is_current; bool is_save_point; bool on_redo_path; bool read_only; }`. One row per shown change. `graph` is what comes before the id, as the module's layout describes (`│ ` per enclosing level, `├─` or `│ ` for its own, `○` or `●`, `>` while closed, a space). `depth` is the branch nesting, two columns a level, so the `○` is at column `2 × depth`. `expandable` marks a change with side branches, or an older tree's closed row; `expanded` says they are shown. `branch_from` is the change the row's branch split from (none on a tree's outer line), where Left goes. `on_redo_path` marks nodes reachable from `current` by following preferred children, which is where redo would go. `read_only` marks rows of other trees and retired trees; they are drawn dimmed.

Order, so that two implementations draw the same rows: trees as the module says; within an open tree, depth first along lines: a line's changes top to bottom, and under each open change its side branches in ascending `NodeId`, each in full (with its own open branches), before the line goes on with the change's newest child.

- **Access:** internal to the panel; public only so that tests can inspect the row list.

## class: HistoryView

- **Inputs:** `now_ms`: an optional clock returning Unix milliseconds, for the relative times (default: the system clock); tests inject one. The tree is passed to `open`, and the panel keeps a reference to it while open: App closes the panel before anything replaces the tree, and opens it again on the same tree straight after a clear or a prune, before the next frame is drawn.
- **State changes:** `closed | open(rows, selected, scroll)`. Invariants while open: `rows` are the shown changes in the order of [HistoryRow](#symbol-historyrow), given the open changes and open trees; `selected` indexes a row; the selected row is inside the visible area after every key, and stays on the same change when branches open or close.
- **Owns:** see the module.
- **Access:** App routes keys here while it is open and no prompt is. A prompt opened from the panel (by C or P) takes the keys until it closes, with the panel still drawn under it.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [history_view (implementation)](./history_view.cpp.skel.md)
- **Referred by:** [history_view_test](../../tests/history_view_test.cpp.skel.md)

### function: open

- **Inputs:** `tree`: a `const UndoTree&`.
- **Returns:** nothing.
- **State changes:** closes every branch and older tree, opens the changes the current state's path branched off from, builds the row list from every root in `tree.roots()` and `tree.retired_roots()` as [HistoryRow](#symbol-historyrow) describes, selects the current node's row (the last row when there is none), and scrolls it into view.
- **Access:** App, on `UndoHistory`.

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** a `HistoryKeyResult { std::optional<NodeId> jump; bool closed; std::string message; std::optional<CommandId> command; }`: `jump` is the node to jump to when the user pressed Enter; `closed` is true when the key closed the panel (Esc); `message` is a refusal to show on the status line, or empty; `command` is `ClearHistory` or `TrimHistory` when the user pressed C or P.
- **State changes:**
  - Up and Down move the selection one row; PageUp and PageDown move it one screen; Home and End go to the first and last row; all clamp at the ends.
  - Right, like the folder tree's, on a change with closed side branches opens them (on an older tree's closed row, opens the tree); on an open one moves to the next row, its first branch; otherwise does nothing.
  - Left on a change with open side branches closes them; else, inside a branch, moves to the change the branch split from; else, on the outer line of an open older tree, closes the tree and selects its row; otherwise does nothing.
  - Enter on a `read_only` row refuses with a status message and returns `nullopt`; the panel stays open.
  - Enter on any other row returns the selected node. App calls `Document.jump_to` with it, and closes the panel on success. On failure the panel stays open and the status line shows the reason.
  - Esc closes the panel without changing the document.
  - C, T and P, in either case and without Ctrl or Alt, return `ClearHistory`, `TrimHistory` and `TogglePersistHistory` in `command`; the panel stays open and App runs the command.
  - Every other key is consumed and ignored, so typing cannot reach the document while the panel is open.
- **Access:** App.
- **Depends on:** [CommandId](../app/commands.hpp.skel.md#symbol-commandid)

### function: row_text

- **Inputs:** `index`: a row index.
- **Returns:** the row's full text before truncation: `graph`, then for a node row the id, kind, save marker and relative time as the module describes.
- **State changes:** none.
- **Access:** `render`; tests.

### function: rows

- **Inputs:** none.
- **Returns:** the row list, `const std::vector<HistoryRow>&` (empty while closed), and `selected()` the selected row index.
- **State changes:** none.
- **Access:** tests.

### function: render

- **Inputs:** a `Screen&`; `area`: the rectangle App gives the pane, inside the frame, `history_pane_width(terminal columns)` wide; `focused`: whether the pane has the keys (false while the previewed text has them, after Tab; true by default).
- **Returns:** nothing.
- **State changes:** draws the visible rows with [attr_for](./theme.hpp.skel.md#function-attr_for): the folder tree's look: plain (`Default`) for rows, `list_selected` for the selected row while `focused` and `list_selected_unfocused` otherwise, the footer's commands and rule in `menu`, `gutter_current` for the current node's `*` marker, and `history_read_only` (dim, an attribute rather than a color) for `read_only` rows. No new colors. The frame and the status-line hint are drawn by App.
- **Access:** App.render.

### function: close

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** discards the row list and the tree reference and goes to `closed`. `is_open()` reports the state.
- **Access:** App, after a successful jump, on Esc, and when the document is replaced or reloaded underneath it (File > Open cannot happen while it has focus, but a reload can be pending).
