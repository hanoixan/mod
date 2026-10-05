---
role: test
stamp: source 649a9cf3, stand-in d5f9ce56
---
# module: history_view_test

Row building for: a single root; a linear chain of 1 000 000 nodes (no recursion, one lane throughout); the rows of the layout sketch in [history_view](../src/ui/history_view.hpp.skel.md): nodes 11–14, with 13 and 14 both children of 12, 14 current and no save points, give rows whose graph, id and kind prefixes are `* 14 typed`, `| o 13 paste`, `|/`, `o 12 typed` and `o 11 delete` (the relative time after them comes from an injected clock); nested and parallel branches (lanes open and close as specified, indentation grows with open branches only); newest-first order. The current and save-point markers and the redo path. A forest after a reload and after Clear History: the current tree first, other and retired trees after it, their rows `read_only`; Enter on a `read_only` row refuses and the panel stays open. Selection movement skipping connector rows, and clamping at both ends. Enter on a node returns it; Esc returns nothing and leaves the document untouched. Together with [Document.jump_to](../src/edit/document.hpp.skel.md#function-jump_to): jumping across branches through the lowest common ancestor gives the same content as the equivalent undo and redo sequence; the next edit after a jump creates a new child of the chosen node; a jump to a node that predates verification while `verifying` is refused. [history_pane_width](../src/ui/history_view.hpp.skel.md#function-history_pane_width) at 59 (57, full width), 60 (20), 80 (26), 119 (39), 120 (40) and 200 (40) columns, and `render` truncating a long row to the given area. After [Document.prune_history](../src/edit/document.hpp.skel.md#function-prune_history), `open` shows one tree whose root is the anchor, with no `read_only` rows for the removed history (retired rows from an earlier Clear History still appear dimmed), and Enter on a top from another kept branch jumps there through the new root. The footer: a rule and Clear History… and Trim History… at the bottom of the pane with C and P underlined; the list scrolls within the rows above it; a pane under five rows has no footer; C and P (either case, not with Alt) return `ClearHistory` and `TrimHistory` and leave the panel open, and Enter returns no command. The footer's three rows (Clear History…, Trim History…, the Persist History checkbox drawn as `[x]` or `[ ]`), and C, T and P returning their commands. The checkbox shows the state given with `set_persist`. `selected_node()` follows the selection and is empty on connector and read-only rows. The footer's rule is a single box-drawing line. An empty history takes every key without reading past its rows; Escape and the letters still work.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Relative times in rows use an injected clock.
- **Depends on:** [HistoryView](../src/ui/history_view.hpp.skel.md#class-historyview)
- **Depends on:** [Document.jump_to](../src/edit/document.hpp.skel.md#function-jump_to)
- **Depends on:** [history_pane_width](../src/ui/history_view.hpp.skel.md#function-history_pane_width)
- **Depends on:** [Document.prune_history](../src/edit/document.hpp.skel.md#function-prune_history)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
