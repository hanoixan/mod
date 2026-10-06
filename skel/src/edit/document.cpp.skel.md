---
role: product
unit: ./document.hpp.skel.md
stamp: source 9b7838cb, stand-in 86776aa3
---
# module: document (implementation)

Implements [Document](./document.hpp.skel.md#class-document). It holds the wiring between the scanner, the sidecar and the tree:

- The scanner's `on_chunk` calls `PieceTree.record_chunk_lines`.
- The scanner's `on_done(hash)` decides history. It runs exactly once per open or [reload](./document.hpp.skel.md#function-reload), even if a save happened during the scan. After a reload it always takes the no-history branch below. A `verifying` state calls `Sidecar.verify`:
  - On a match, the state becomes `attached`.
  - On a mismatch, session nodes (in memory only, because sidecar appends were deferred) are re-parented in the `UndoTree` under a fresh root built from the hash ([UndoTree.reroot](./undo_tree.hpp.skel.md#function-reroot)). The root's id was reserved with `reserve_id` when the provisional match was made, before any session node was created, so every re-parented node keeps `parent < id`. Held-back SAVE and POSITION records that name the candidate are remapped to the new root. The old trees stay in the sidecar but are unreachable this session.
  - With no history, the in-memory root created at open gets its hash and `append_root` is called.

  After that, `Sidecar.release_deferred` writes the queued records with their final parents. A scan that fails (`on_done` with an error) leaves the base hash unknown: a `verifying` document becomes `session_only` (its held-back records are discarded, so nothing unverified is written), and a pending ROOT is never written.
- A reload or Clear History while verifying abandons the verification: the old scanner's results are ignored by generation, and the held-back records are discarded with `discard_deferred` (for Clear History they are dropped by `Sidecar.clear` anyway).
- Sidecar `on_payload_written` calls `UndoTree.rebind_payload`.
- `prune_history`'s silent walk is a private helper beside the undo and redo appliers. It shares their op application, but sends no notifications and does not move `current`. The kept erased runs are its only memory cost, and they are released when the walk back finishes.

Generation numbers in posted closures protect against stale callbacks after a reopen or save.

- **Owns:** the closure wiring and generation counters.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** session edits made while `verifying` must be re-rooted, not dropped. Test this explicitly.
- **Depends on:** [Document](./document.hpp.skel.md#class-document)
- **Depends on:** [LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner)
- **Depends on:** [Sidecar.verify](./sidecar.hpp.skel.md#function-verify)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
