---
role: product
unit: ./undo_tree.hpp.skel.md
stamp: source 9340c830, stand-in 24db03a6
---
# module: undo_tree (implementation)

Implements [UndoTree](./undo_tree.hpp.skel.md#class-undotree). Nodes are stored in a `std::vector` with a `std::unordered_map<NodeId, index>` alongside. Child lists are kept sorted by id.

- **Owns:** node storage.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** recursion depth in `path` on very deep histories (millions of nodes in a line). Walk the parents iteratively, never recursively. The same applies to `plan_prune`, whose leaf walks and lowest-common-ancestor search touch every node at most a constant number of times (mark kept nodes, then climb from the tops' parents with depth counts), and to `apply_prune`, which compacts the node vector and rebuilds the id index in one pass.
- **Depends on:** [UndoTree](./undo_tree.hpp.skel.md#class-undotree)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
