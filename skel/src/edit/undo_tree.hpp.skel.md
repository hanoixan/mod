---
role: product
stamp: source f8991134, stand-in dd72077e
---
# module: undo_tree

An unlimited, automatically branching undo history. Each node is one user action, stored as self-contained invertible edit ops. Editing after an undo adds a new child, so the old redo path is never discarded and becomes a sibling branch. Redo follows each node's *preferred child*: the most recently created or visited one. The tree may be a forest: a new root is started whenever the file on disk cannot be matched to existing history. See [Sidecar](./sidecar.hpp.skel.md#class-sidecar).

This module is pure in-memory bookkeeping. It never touches the piece tree or the disk. [Document](./document.hpp.skel.md#class-document) applies ops, and [Sidecar](./sidecar.hpp.skel.md#class-sidecar) persists them.

- **Owns:** all nodes, their ops, payload descriptors, save-point marks, and the `current` pointer. It also owns the prune rule: which nodes are kept, and how they are re-joined.
- **Access:** public. Owned by `Document`. Main thread only.
- **Required:** always.
- **Failure modes:** memory growth from inline payloads. Payloads above the inline threshold become `SidecarRef`s once persisted, and are kept as `PieceRun`s until then (cheap). Node metadata is about 100 bytes per node and stays in memory, which allows millions of nodes. A corrupt parent link from a bad sidecar can create cycles or orphans: `load_node` rejects any node whose parent is unknown or whose id is not greater than its parent's.
- **Depends on:** [PieceRun](../text/piece_tree.hpp.skel.md#symbol-piecerun)
- **Depends on:** [ContentHash](../util/hash.hpp.skel.md#symbol-contenthash)

Branches are navigated in two ways. Edit > Next Branch and Edit > Previous Branch change the preferred child of the current node one step at a time (menu-only). The undo-history panel, [HistoryView](../ui/history_view.hpp.skel.md#class-historyview), shows the whole forest (other roots dimmed and read-only) and jumps to any node in the current root's tree through [Document.jump_to](./document.hpp.skel.md#function-jump_to), which walks [path](#function-path) with [follow](#function-follow). It is opened from the Edit menu only.

**Pruning.** A history can be cut down by age with [plan_prune](#function-plan_prune) and [apply_prune](#function-apply_prune), for the Undo History pane's Trim History… and for the offer made when a sidecar is slow to load (see [Document.prune_history](./document.hpp.skel.md#function-prune_history)). Unlike [reset](#function-reset), pruning deletes nodes outright: they are not retired, not shown dimmed, and nothing about them survives. The steps that are kept stay reachable from a new root, so jumping between them still works.

- **Unknowns:** none in this module.

## symbol: NodeId

`uint64_t`. Ids are assigned in increasing order across sessions: on load, the next id is the largest stored id plus 1. Roots have `parent == kNoParent` (all bits set, a public constant). Ids need not be contiguous: [reserve_id](#function-reserve_id) can skip one.

- **Access:** public.

## symbol: Payload

The bytes removed or inserted by one op, stored in exactly one of three forms:

- `Inline { std::string bytes }`: at most `kInlineMax` = 4 KiB (4096 bytes, a public constant). Typed text is always inline.
- `Pieces { PieceRun run; uint64_t length }`: an in-session reference into immutable buffers, with no copy. Used for large deletions, cut and paste until persisted.
- `SidecarRef { uint64_t file_offset; uint64_t length }`: bytes stored in the sidecar's payload area. They are materialized for the piece tree through the sidecar mapping.

A payload always knows its `length()` without materializing. In C++ it is a struct wrapping `std::variant<Payload::Inline, Payload::Pieces, SidecarRef>`; `SidecarRef` is a namespace-level struct because [Document.apply](./document.hpp.skel.md#function-apply) also accepts it. `Which` (`removed` or `inserted`) names one of an op's two payloads.

- **Access:** public.

## symbol: EditOp

`{ uint64_t offset; Payload removed; Payload inserted; }`.

- Applying it means erasing `removed.length()` bytes at `offset`, then inserting `inserted` at `offset`.
- Inverting it means erasing `inserted.length()` bytes at `offset`, then inserting `removed`.

The ops in a node are applied in order and inverted in reverse order. Offsets refer to the document as it is just before that op.

- **Access:** public.

## symbol: NodeMeta

`{ NodeId id; NodeId parent; int64_t time_unix_ms; EditKind kind; uint64_t cursor_before; uint64_t cursor_after; }`. `EditKind` is one of `typing`, `delete`, `paste`, `cut`, `replace`, `replace_all`, `newline`, `indent`, `other`, with the numeric values 0 to 8 in that order (they are the sidecar's `kind` byte). `delete` is a C++ keyword, so that enumerator is spelled `delete_`.

- **Access:** public.

## class: UndoTree

- **Inputs:** none. An empty forest has no current node until `add_root`.
- **State changes:** invariants:
  - `current` is always a valid node once a root exists.
  - Each parent's `preferred_child` is one of its children, or none.
  - `commit` makes the new node its parent's preferred child.
  - `undo_step` and `redo_step` move `current` by exactly one edge.
  - The node-to-root path from `current` determines the document content, given the root's base file.
  - Retired nodes (see `reset`) are never `current`, never in a `path`, and hold no ops.
- **Owns:** nodes, keyed by `NodeId`. Small integer-indexed storage is suggested.
- **Access:** `Document` only.
- **Referred by:** [document](./document.hpp.skel.md)
- **Referred by:** [sidecar](./sidecar.hpp.skel.md)
- **Referred by:** [undo_tree (implementation)](./undo_tree.cpp.skel.md)
- **Referred by:** [undo_tree_test](../../tests/undo_tree_test.cpp.skel.md)

### function: add_root

- **Inputs:** `meta`: a root's `NodeMeta`, with no ops; `base_size`, `base_hash`: the file state this root represents.
- **Returns:** the `NodeId`.
- **State changes:** creates the root and makes it `current`.
- **Access:** Document, on first open without history, or after a mismatch or external change.

### function: commit

- **Inputs:** `ops`: non-empty, already applied to the document; `meta`: with `parent` ignored and set to `current`.
- **Returns:** the new `NodeId`.
- **State changes:** adds a child of `current`, sets the preferred child, and moves `current` to it.
- **Access:** Document, when it closes a coalescing group.

### function: amend_current

- **Inputs:** `op`; `cursor_after`.
- **Returns:** nothing.
- **State changes:** appends an op to the current node. Only valid while that node is the open coalescing group and has not yet been persisted. When the op directly continues the node's last op (an insertion right after the last insertion, or a deletion just before or at the last deletion), both `Inline`, and the merged payload stays within `kInlineMax`, the two are merged into one op; the effect of applying the node is the same, and a typing burst does not cost one op per character. Also updates the node's `cursor_after`.
- **Access:** Document typing coalescing.

### function: undo_step

- **Inputs:** none.
- **Returns:** the node to invert, or `nullopt` at a root.
- **State changes:** moves `current` to the parent, and sets the parent's preferred child to the node just left. This is what makes redo "go back where you were".
- **Access:** [Document.undo](./document.hpp.skel.md#function-undo).

### function: redo_step

- **Inputs:** none.
- **Returns:** the preferred child to apply, or `nullopt` at a leaf.
- **State changes:** moves `current` to that child.
- **Access:** [Document.redo](./document.hpp.skel.md#function-redo).

### function: cycle_branch

- **Inputs:** `direction`: +1 or −1.
- **Returns:** the `(index, count)` of the newly preferred child among `current`'s children, for the status line ("branch 2/3"). It returns `nullopt` if there are fewer than 2 children.
- **State changes:** changes `current`'s preferred child in sibling order, which is ascending `NodeId` (creation time).
- **Access:** Document, from the Edit > Next/Previous Branch commands.

### function: path

- **Inputs:** `from`, `to`: nodes in the same root's tree.
- **Returns:** a `std::vector<PathStep>`, where `PathStep` is `{node, direction: undo|redo}`, through their lowest common ancestor: first one `undo` step per node from `from` up to (not including) the ancestor, each naming the node being left; then one `redo` step per node from the ancestor's child down to `to`, each naming the node being entered. Empty when `from == to`. Parents are walked iteratively.
- **State changes:** none.
- **Access:** Document, when jumping to a save point while loading history, and [Document.jump_to](./document.hpp.skel.md#function-jump_to).

### function: follow

Moves `current` along one edge chosen by the caller, rather than along the preferred child.

- **Inputs:** `step`: one element of a `path` result, `{node, direction}`. For `undo`, `node` must be `current`; for `redo`, `node` must be a child of `current`.
- **Returns:** the changed `(parent, preferred_child)` pair, a `PreferredChange`.
- **State changes:** for `undo`, as `undo_step`: `current` moves to the parent, whose preferred child becomes the node just left. For `redo`, `current` moves to `node`, which becomes its parent's preferred child. Either way, the changed `(parent, preferred_child)` pair is reported to the caller for the POSITION record.
- **Access:** [Document.jump_to](./document.hpp.skel.md#function-jump_to) only. Calling it with a step that does not start at `current` is a programming error (assertion).

### function: node_info

Read-only inspection for the history panel.

- **Inputs:** `node`.
- **Returns:** `{ const NodeMeta& meta; std::span<const NodeId> children; std::optional<NodeId> preferred_child; bool is_save_point; uint32_t op_count; }`. `children` is in ascending `NodeId`. Also available: `roots()`, the live root ids in creation order, `retired_roots()`, the roots retired by `reset` in creation order, `root_of(node)`, `is_retired(node)`, and `current()`. For a retired node, `preferred_child` is empty and `op_count` is 0.
- **State changes:** none.
- **Access:** [HistoryView](../ui/history_view.hpp.skel.md#class-historyview), read-only, on the main thread. The returned span is invalidated by the next mutation of the tree.
- **Referred by:** [HistoryView](../ui/history_view.hpp.skel.md)

### function: reset

Retires the whole forest, for Clear History….

The cleared history stays **visible** in the undo-history panel for the rest of the session, dimmed and read-only, but none of its text survives: only the metadata needed to draw it is kept.

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** moves every node into the *retired* set and releases everything else about it: every op and payload (`Inline` bytes freed, `Pieces` and `SidecarRef` references dropped), save-point sizes and hashes, and preferred-child links. A retired node keeps only its `NodeMeta` (id, parent, time, kind, cursor offsets; never any document text), its children list, and whether it was a save point. Retired nodes are never written to any sidecar, so they disappear when the session ends. The live forest is empty, with no `current`, until the caller's next `add_root`. Node ids are **not** reused: the next id continues from the largest id ever assigned in this process, so stale ids held elsewhere (a closure posted by the sidecar writer before the reset) can be recognized and dropped.
- **Access:** [Document.clear_history](./document.hpp.skel.md#function-clear_history) only, immediately followed by `add_root`.
- **Failure modes:** memory: retired metadata costs about 100 bytes per node for the rest of the session, which is accepted. A second Clear History retires the new forest alongside the first; both stay visible.

### function: mark_saved

- **Inputs:** `node`, `size`, `hash`.
- **Returns:** nothing.
- **State changes:** records a save point. The latest save point of this session becomes `saved_node` for dirty tracking.
- **Access:** [Document.save](./document.hpp.skel.md#function-save) and sidecar load.

### function: is_at_saved

- **Inputs:** none.
- **Returns:** `current == saved_node`. Undoing back to the saved state makes the document clean again.
- **State changes:** none.
- **Access:** Document.is_dirty.

### function: load_node

- **Inputs:** `meta`, `ops`, from a NODE record.
- **Returns:** `Status`, which is `format` on an invalid parent, an id that is not new, or an id above `kMaxNodeId` (2^62: far beyond any real history, and far enough below `kNoParent` that a crafted sidecar cannot make new ids wrap or alias).
- **State changes:** inserts the node without moving `current`, makes it its parent's preferred child (as `commit` would have; later POSITION records override that through `set_position`), and raises the next id past it.
- **Access:** [Sidecar.open](./sidecar.hpp.skel.md#function-open) during load.

### function: ops

- **Inputs:** `node`.
- **Returns:** `std::span<const EditOp>`: the node's ops in application order; empty for a root or a retired node. Invalidated by the next mutation.
- **State changes:** none.
- **Access:** Document (to apply and invert nodes, and to hand closed nodes to the sidecar) and [Sidecar.copy_to](./sidecar.hpp.skel.md#function-copy_to).

### function: root_base

- **Inputs:** `root`.
- **Returns:** `std::optional<{ uint64_t size; ContentHash hash; }>`: the base file state of a live root, `nullopt` for any other node. `save_point(node)` returns the latest recorded `{size, hash}` of a live save point the same way. `save_order(node)` returns a number that increases with every `mark_saved`, taken by the node's latest one, or 0 for a node that is not a live save point; it orders save points as their SAVE records were written, for [plan_prune](#function-plan_prune)'s latest save point and [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite). `meta(node)` returns the `NodeMeta`.
- **State changes:** none.
- **Access:** Document and Sidecar.

### function: load_root

- **Inputs:** `meta` (with `parent == kNoParent`), `base_size`, `base_hash`, from a ROOT record.
- **Returns:** `Status`: `format` when the id is not new or is above `kMaxNodeId`.
- **State changes:** inserts the root without moving `current`, and raises the next id past it.
- **Access:** [Sidecar.open](./sidecar.hpp.skel.md#function-open) during load.

### function: set_position

- **Inputs:** `current`: an optional node; `preferred_changes`: `(parent, child)` pairs.
- **Returns:** nothing.
- **State changes:** applies the preferred children whose parent and child are live and related (others are ignored), and when `current` is given and live, makes it `current`. Used by load (POSITION records, and the candidate chosen by Document) only; editing never jumps `current` this way.
- **Access:** Sidecar.open and Document.open.

### function: set_base_hash

- **Inputs:** `root`: a live root created before its hash was known; `hash`.
- **Returns:** nothing.
- **State changes:** sets the root's base hash (and its in-memory save-point hash, when the root is the save point of its unchanged base).
- **Access:** [document (implementation)](./document.cpp.skel.md), when the scanner's hash arrives for a root created at open, reload or Clear History.

### function: reserve_id

- **Inputs:** none.
- **Returns:** a fresh `NodeId` that no later node will get.
- **State changes:** advances the next id.
- **Access:** Document, when it opens with a provisional match: the reserved id becomes the new root's id if verification fails (see [reroot](#function-reroot)), which keeps `parent < id` for the re-parented session nodes.

### function: reroot

Moves the session's nodes to a new root after a verification mismatch.

- **Inputs:** `old_parent`: the provisional candidate; `first_session_id`: the smallest id created this session; `root_id`: the id from `reserve_id`; `meta`, `base_size`, `base_hash`: the new root.
- **Returns:** nothing.
- **State changes:** creates the root with `root_id`, moves every child of `old_parent` whose id is at least `first_session_id` under it (keeping the preferred child among them), and when `current` or the session's save point was `old_parent`, moves it to the new root, whose save point then takes the recorded size and hash. The old tree keeps its loaded nodes.
- **Access:** [document (implementation)](./document.cpp.skel.md) only.

### function: visit_pieces

- **Inputs:** `visit`: a callback `(NodeId, uint32_t op_index, Which, PieceRun&) -> bool`.
- **Returns:** nothing.
- **State changes:** calls `visit` for every live `Pieces` payload; the callback may replace the run in place with an equivalent one of the same length (returning true). Retired nodes have no payloads.
- **Access:** [Document.save](./document.hpp.skel.md#function-save), to materialize payloads before an in-place write.

### function: rebind_payload

- **Inputs:** `node`, `op_index`, `which`: removed or inserted; `ref`: a `SidecarRef`.
- **Returns:** nothing.
- **State changes:** replaces a `Pieces` payload with a `SidecarRef`, which releases references to old buffers. After a prune, it also replaces a `SidecarRef` with the new one into the rewritten sidecar. A `node` that was retired by `reset` or deleted by `apply_prune` is ignored, and so is an `op_index` past the node's last op.
- **Access:** Document, in a closure posted by the sidecar writer, and [Document.prune_history](./document.hpp.skel.md#function-prune_history) with the refs returned by [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite).

### function: oldest_change

- **Inputs:** none.
- **Returns:** `std::optional<int64_t>`: the smallest `time_unix_ms` among the non-root nodes of the whole live forest, or `nullopt` when no live tree has a change. Retired nodes don't count. The whole forest is used because a prune deletes the other live trees too. After a load that matched no save point, every loaded change is in another tree, while the current tree is a new, empty root. The prune prompt shows this time as an age in whole days.
- **State changes:** none.
- **Access:** [Document.prune_preview](./document.hpp.skel.md#function-prune_preview).

### function: plan_prune

Decides what a prune keeps. The user's rule: every leaf is walked back towards the root to the farthest ancestor that is not too old, or that is already included. Those farthest ancestors are joined as the children of one new root, which takes the age of the oldest of them. Everything else is deleted.

- **Inputs:** `cutoff_ms`: Unix milliseconds. A non-root node is *too old* when `time_unix_ms < cutoff_ms`. The caller computes the cutoff as `now − days × 86 400 000`.
- **Returns:** a `PrunePlan`. A plan with `removed_count == 0` means that there is nothing to prune.
- **State changes:** none.
- **Access:** Document only. Precondition: a live `current` exists. Document refuses to prune while verifying, so the plan never meets nodes that a re-root could still move.

#### Rule

Let *T* be the current root's tree, *R* its root, and *F* the **forced** set: `current` and the latest save point in *T* (the session's `saved_node` when it is in *T*, otherwise the save point in *T* recorded last). Forced nodes are kept even when they are too old. This is the assumption the user accepted: the step the document is at, and its latest save point, are always kept.

1. **Kept nodes.** The walk starts from every leaf of *T* and every node of *F*, in ascending `NodeId`. A start that is too old and not forced contributes nothing. Otherwise the start is kept, and the walk moves to its parent. It keeps that parent and continues while the parent is not *R*, not too old, and not already kept. It stops at the first node that fails one of these tests. A forced node only exempts itself: its ancestors are kept only when they are recent. The walk is iterative.
2. **Tops.** The tops are the kept nodes whose parent is not kept.
3. **Anchor.**
   - If *R* is forced (`current` or the latest save point is the root itself), or nothing was kept, the anchor is *R*.
   - Otherwise the anchor is the deepest node that is a proper ancestor of every top: the lowest common ancestor of the tops' parents. With one top, that is its parent.
   - The anchor is never a kept node. When the walk kept a chain of recent nodes above an old node that has recent descendants (recent → old → recent), both chains have tops. The anchor lies above both, so the nested top becomes a sibling of the outer one. This flattening is intended: every kept step stays reachable.
4. **What is deleted.** Every node of *T* that is neither kept nor the anchor is deleted, and *R* is deleted when the anchor is not *R*. Every other live tree (histories from before a reload or a verification mismatch) is deleted whole: its base content is not available, so none of it can be reached anyway. Retired metadata from an earlier Clear History in the same session is **not** touched. It is session-only, it is never written to a sidecar, and it is not pruned history.

#### Worked example

The new root is the content of the anchor. Each top's first step absorbs the deleted steps between the anchor and itself.

```text
R ── a ── b ──┬── c ── d          too old: a, b, e     recent: c, d, f
              └── e ── f          leaves: d, f         current: d; latest save point: c
```

The kept nodes are `{c, d, f}`, and the tops are `c` and `f`. The tops' parents are `b` and `e`, whose lowest common ancestor is `b`. The anchor is therefore `b`. The new root has `b`'s id and content and takes the time of the older of `c` and `f`. `c` keeps its own ops. `f` absorbs `e`: its ops become `e`'s ops followed by `f`'s. `R` and `a` are deleted, and `e` is deleted as a node.

### function: for_each_pruned

- **Inputs:** `plan`; `visit`: a callback `(const PrunedNode&) -> bool` that returns false to stop.
- **Returns:** nothing.
- **State changes:** none. Calls `visit` once per kept node, in ascending `NodeId`, so every parent comes before its children. For a top, `meta.parent` is the anchor's id, and `op_runs` holds the ops of every node on the path from the anchor's child down to the top inclusive, in path order. `meta.cursor_before` is the first of those nodes' `cursor_before`, and `time_unix_ms`, `kind` and `cursor_after` are the top's own. Any other kept node is visited unchanged, with one run.
- **Access:** [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite), which serializes the pruned history before anything in memory changes, and `apply_prune`.

### function: pruned_preferred

- **Inputs:** `plan`.
- **Returns:** `std::vector<PreferredChange>`: every `(parent, preferred_child)` pair that [apply_prune](#function-apply_prune) will leave, the new root's first when it has one. It is what a rewritten sidecar's POSITION record holds.
- **State changes:** none.
- **Access:** [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite), and `apply_prune` itself, so the two cannot disagree.

### function: apply_prune

- **Inputs:** `plan`, from `plan_prune` on the unchanged tree; `base_size` and `base_hash`: the anchor's content.
- **Returns:** nothing.
- **State changes:**
  - Replaces the current root's tree and every other live tree with the pruned tree. The new root has the anchor's `NodeId`, `parent == kNoParent`, `time_unix_ms = plan.root_time_ms`, the given base, no ops, `kind` `other` and cursors 0, like every other root (a ROOT record stores no kind, so a reloaded root has `other` too).
  - Tops are re-parented under the new root, and their ops become the concatenation that `for_each_pruned` describes. Payloads of removed nodes are moved, not copied. Two exceptions are copied: the ops of a kept node that a nested top absorbs (the kept node keeps its own), and the ops of a removed node that lies on the path of several tops (when the anchor is above the node where their paths split).
  - Every other node of the old trees is deleted, with its ops and payloads, and is never retired.
  - Save-point marks on kept nodes stay. When the anchor was a save point, the new root is marked saved with that save point's size and hash. `saved_node` and `current` do not change, because both are kept. If either was *R* and *R* is the anchor, it is the new root.
  - Preferred children: a kept node keeps its preferred child when that child is kept. Otherwise its preferred child is its most recently created kept child, or none. The new root's preferred child is the top on the path to `current`, or none when `current` is the root.
  - `roots()` then lists exactly the new root. The next id is unchanged, and ids are never reused.
  - Retired nodes are untouched.
- **Access:** [Document.prune_history](./document.hpp.skel.md#function-prune_history) only, after the sidecar has been rewritten successfully.
- **Failure modes:** a plan from a changed tree is a programming error, asserted in debug builds. Document computes and applies the plan with no edit in between.

## symbol: PrunePlan

The result of [plan_prune](#function-plan_prune): which nodes a prune keeps and how they are joined. It names nodes only and holds no payloads. Any structural mutation of the tree invalidates it (`commit`, `add_root`, `reset`, a load or a re-root); rebinding payloads does not.

`{ int64_t cutoff_ms; NodeId anchor; bool anchor_is_root; std::vector<NodeId> tops; std::vector<NodeId> kept; int64_t root_time_ms; uint64_t removed_count; uint64_t removed_trees; }`

- `kept`: the kept non-root nodes of the current root's tree, in ascending `NodeId`.
- `tops`: the kept nodes whose parent is not kept, in ascending `NodeId`. They become children of the new root.
- `anchor`: the node whose content becomes the new root's base content. The new root reuses its `NodeId`. `anchor_is_root` is true when it is the current tree's existing root.
- `root_time_ms`: the time the new root takes, which is the oldest `time_unix_ms` among `tops`. When `tops` is empty, it is the anchor's own time.
- `removed_count`: every live node deleted by the prune, including the old root when `anchor` is not the root, and every node of the other live trees. `removed_trees` counts those other trees.

- **Access:** public; Document, Sidecar and tests.

## symbol: PrunedNode

`{ NodeMeta meta; std::vector<std::span<const EditOp>> op_runs; }`: one kept node as it will be after the prune. `meta.parent` and `meta.cursor_before` already hold their new values. `op_runs` are spans of the existing nodes' ops, in application order. A node's pruned ops are the concatenation of its runs, and an op's *pruned index* is its position in that concatenation. The spans are invalidated by the next mutation of the tree.

- **Access:** public; [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite) serializes these, and `apply_prune` installs them.
