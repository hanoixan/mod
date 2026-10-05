---
role: product
unit: ./history_view.hpp.skel.md
stamp: source 47e64cf4, stand-in 20cc2e83
---
# module: history_view (implementation)

Implements [HistoryView](./history_view.hpp.skel.md#class-historyview). The row builder never recurses, because a linear history can be millions of nodes deep and branches can nest deeply. Per open tree it walks lines with an explicit stack of pending lines over [UndoTree.node_info](../edit/undo_tree.hpp.skel.md#function-node_info): along a line it follows the newest child; at an open change it pushes the rest of its line, then its side branches in reverse, so they come out oldest first. The rows are rebuilt from the open sets whenever a branch or tree opens or closes, keeping the selected change. Cost is O(shown rows) per build; an older tree's closed row walks its newest line once.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** stack overflow from a recursive walk on a deep history; avoided by the explicit stack. Deeply nested open branches make a row's `graph` string long; it is built when the rows are, not per frame.
- **Depends on:** [HistoryView](./history_view.hpp.skel.md#class-historyview)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
