---
role: product
unit: ./history_view.hpp.skel.md
stamp: source b4a141bd, stand-in 2ef3dfe2
---
# module: history_view (implementation)

Implements [HistoryView](./history_view.hpp.skel.md#class-historyview). The row builder never recurses, because a linear history can be millions of nodes deep. Per tree, it collects the tree's node ids with an explicit-stack walk over [UndoTree.node_info](../edit/undo_tree.hpp.skel.md#function-node_info), sorts them in descending `NodeId`, and assigns lanes in one pass as [HistoryRow](./history_view.hpp.skel.md#symbol-historyrow) specifies, keeping the open lanes in a small vector. Cost is O(nodes × open lanes) per open, plus the sort.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** stack overflow from a recursive walk on a deep history; avoided by the explicit stack. Many simultaneously open lanes make each row's `graph` string long; it is built once per open, not per frame.
- **Depends on:** [HistoryView](./history_view.hpp.skel.md#class-historyview)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
