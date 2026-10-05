---
role: test
stamp: source 6166f92b, stand-in eb8c4fda
---
# module: undo_tree_test

Branching: undo, new edit, a sibling is created, and redo follows the preferred child. Also: `cycle_branch`; the `path` LCA; `follow` along a non-preferred child sets the preferred child; `reset` followed by `add_root`, with ids not reused, a stale `rebind_payload` ignored, the old nodes listed by `retired_roots()` with metadata intact and no ops or payloads, and `path` never reaching a retired node; the coalescing timeout boundary (0.999 s coalesces, 1.000 s starts a new node, with an injected clock); `is_at_saved` after undoing back to a save; Document-level coalescing rules; re-rooting session edits on a verify mismatch; and `Document.reload`, which adds a new root that undo cannot leave, discards a pending verification, and writes its ROOT record only after the new scan's hash arrives. `Document.clear_history` on a clean document: one root, still clean, undo refused at it; refused in `read_only`. `Document.open_untitled`: empty, clean, `session_only`, root hash equal to SHA-256 of empty input.

Pruning, with an injected clock:
- **`plan_prune` structure.**
  - The worked example in [plan_prune](../src/edit/undo_tree.hpp.skel.md#function-plan_prune) gives kept `{c, d, f}`, tops `{c, f}`, anchor `b`, and the root time of the older top.
  - A single kept chain anchors at its top's parent.
  - The nested recent → old → recent case flattens to sibling tops under an anchor above both.
  - A walk stops at an already kept node.
  - A leaf too old for the cutoff contributes nothing.
  - An old `current` and an old latest save point are kept, without their old ancestors.
  - `current` at the root anchors at the root.
  - With no other live trees, a cutoff older than every node removes nothing. With other trees, the same cutoff removes exactly those trees.
  - After a load that matched no save point, `oldest_change` reports the oldest change in the other trees.
  - Other live trees are counted in `removed_count` and `removed_trees`.
  - Retired trees from Clear History are untouched.
- **`for_each_pruned`.** It visits the kept nodes in ascending id with the concatenated runs and the first absorbed node's `cursor_before`.
- **`apply_prune`.** `roots()` holds just the anchor's id. Preferred children are as specified. `is_at_saved` is unchanged. The next id is unchanged. A stale `rebind_payload` for a deleted node, or an out-of-range op index, is ignored.
- **`Document.prune_history`.**
  - Content is equal before and after.
  - [Document.jump_to](../src/edit/document.hpp.skel.md#function-jump_to) between every pair of kept nodes gives the same content as before the prune.
  - The anchor's hash is computed by the silent walk when it is not a save point, and no listener sees a change.
  - It is refused while `verifying`, `read_only` and `session_only`, with nothing changed.
  - A rewrite failure leaves the tree and the file unchanged. The test causes it by giving the sidecar a second hard link, so `write_atomically` returns `not_atomic`.
  - `prune_preview` reports the oldest age in whole days and the counts. A document opened with history off reads and writes no sidecar, existing or new, reports session-only history, and shows no status message. Persist History: history kept in memory by default (no sidecar after editing and saving); turning it on writes the whole session's history and a reopen restores it with persistence on; turning it off stops writing and keeps the file; an unreadable sidecar leaves it off, needs an overwrite, and the overwrite writes the session with its save points; a locked sidecar refuses a change; untitled persistence remembered until the first save; Save As without persistence makes no sidecar, and turning it on afterwards writes at the new path. Tests of the written history open with `PersistHistory::always`. Previews: the text at every node of a branched history equals what `jump_to` gives, with the history and the version unchanged; `end_preview` restores the text byte for byte; the marks of an insert, a deletion (spliced back in) and a replacement. Loaded node ids above kMaxNodeId are refused, so a crafted sidecar cannot wrap the id counter. History is saved to `<file>.history`, and an old `<file>.mod` beside the file is never read. A refused prune (a hard-linked history) leaves the file as it was, compared once a first refusal has drained the history writer, so queued records written by that drain are not mistaken for a change.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically. A test that changes a file on disk while a Document still has it open must replace it by rename (a new file renamed over the old one), never truncate it in place: the Document's background scan may still be reading the old mapping, and a mapping of a truncated file raises SIGBUS (the crash [file_map](../src/platform/file_map.hpp.skel.md) records as accepted for real use). The reload test does this.
- **Depends on:** [UndoTree](../src/edit/undo_tree.hpp.skel.md#class-undotree)
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [UndoTree.plan_prune](../src/edit/undo_tree.hpp.skel.md#function-plan_prune)
- **Depends on:** [Document.prune_history](../src/edit/document.hpp.skel.md#function-prune_history)
- **Depends on:** [DocumentOptions](../src/edit/document.hpp.skel.md#class-document)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
