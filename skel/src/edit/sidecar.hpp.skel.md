---
role: product
stamp: source 5a054ff8, stand-in 0ea2572a
---
# module: sidecar

Persists the [UndoTree](./undo_tree.hpp.skel.md#class-undotree) to `<real path>.history` as an append-only, checksummed, little-endian record log. The one exception is a prune, which replaces the whole file with a smaller one; see [rewrite](#function-rewrite). The format is public; see [sidecar-format.md](../../docs/sidecar-format.md.skel.md). Nodes are appended as they are created, not only on save, so a crash loses no history and unsaved edits are recoverable: after reopening, they are reachable with redo from the last save point.

All file writes happen on one writer thread, in FIFO order. The main thread never blocks on sidecar I/O, except in `flush`, which runs before save rebinding and at exit.

- **Owns:** the sidecar file handle, its advisory lock, the writer thread and queue, and a read-only mapping of the file for `SidecarRef` payloads, which is remapped as the file grows.
- **Access:** public. One per Document. Main thread API; the writer thread is internal.
- **Required:** optional — without it undo still works for the session but is not persisted or shareable.
- **Unknowns:** none
- **Failure modes:**
  - The sidecar path exists but is not a sidecar (bad magic), for example editing a file named `go` next to `go.mod`. Never touch it. Run with persistence disabled and show a status message.
  - Another `mod` instance holds the lock (`flock(LOCK_EX|LOCK_NB)` / `LockFileEx`). Load the history read-only and do not append, and show the message "history is open in another mod".
  - The tail record is truncated or fails its CRC after a crash. Ignore it and everything after it, and truncate the file to the last good record before the first append.
  - The disk is full or a write fails mid-session. Disable persistence for the session and keep in-memory history, with a status message. Saving the document itself is not blocked.
  - The version is newer than supported. Open read-only and do not append.
  - The document directory is not writable. No sidecar can be created, so history is session-only.
- **Depends on:** [UndoTree](./undo_tree.hpp.skel.md#class-undotree)
- **Depends on:** [EventQueue.post](../util/event_queue.hpp.skel.md#function-post)
- **Depends on:** [crc32](../util/hash.hpp.skel.md#function-crc32)
- **Depends on:** [MappedFile](../platform/file_map.hpp.skel.md#class-mappedfile)
- **Depends on:** [sidecar_file](../../infra/storage.iac.skel.md#resource-sidecar_file)
- **Depends on:** [sidecar format](../../docs/sidecar-format.md.skel.md)

On File > Save As, the history is **copied** to the new file's sidecar with [copy_to](#function-copy_to), and the old sidecar stays with the old file. From then on the two sidecars share a common prefix and evolve independently.

**Orphans are left alone.** A sidecar orphaned because its document was renamed, moved or deleted by another program is never detected, cleaned up or deleted by `mod`. Its deleted text stays on disk until the user removes the file. A document renamed outside `mod` and then opened under its new name starts with no history, because the sidecar is found by name only.

**Clearing history.** The only way `mod` ever deletes a sidecar is Clear History… in the Undo History pane, through [clear](#function-clear). It deletes this document's sidecar and starts a new history; copies made earlier by Save As next to other file names are not touched.

**Pruning history.** The Undo History pane's Trim History…, or accepting the offer made when loading took more than 5 seconds, replaces the sidecar with a smaller one through [rewrite](#function-rewrite). The pruned history is gone for good. As with Clear History, Save As copies next to other file names are not touched.

**Durability.** The writer thread calls `fsync` (`fdatasync` where available) on the sidecar once per document save: right after it has written the SAVE record that [append_save](#function-append_save) enqueued, and once after [copy_to](#function-copy_to) has written the new sidecar. Records appended between saves are written promptly but never fsynced, so a power failure (not a process crash, which loses nothing that reached `write`) can lose the history recorded since the last save. The main thread never waits for an fsync; `flush` and exit do not fsync.

## symbol: PayloadView

`PayloadView { form: std::string | std::vector<FrozenBytes> | SidecarRef; length; }` and `NodeOp { offset; removed; inserted; }`: an op as handed to the writer thread. The views are worker-safe ([FrozenBytes](../text/piece_tree.hpp.skel.md#symbol-frozenbytes)). A payload of at most `kInlineMax` bytes is written inline in the NODE record whatever its form; a longer one gets a PAYLOAD record. `to_node_ops(ops, text)` converts a node's [EditOp](./undo_tree.hpp.skel.md#symbol-editop)s, turning `Pieces` into views.

- **Access:** public; Document and `copy_to`.

## function: sidecar_path_for

- **Inputs:** `document_path`: an already resolved real path.
- **Returns:** the same directory, with `.history` appended to the *full* file name (`notes.txt` → `notes.txt.history`). Appending avoids clashes between `a.c` and `a.h`. The name is deliberately visible, not a dot-file, so the user can see that history exists and that it holds deleted text.
- **State changes:** none.
- **Access:** public.
- **Depends on:** [resolve_real_path](../platform/fs.hpp.skel.md#function-resolve_real_path)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)

## class: Sidecar

- **Inputs:** `doc_path`: the document's real path, or none for an untitled document; `queue`: an `EventQueue&`; `file_mode`: the document's permission bits, used if the sidecar is created (applied with `fchmod`, so the umask does not mask them); `seams`: a `SidecarSeams { now_ms; write; sync; progress; truncate; }`, defaulted. `now_ms` returns Unix milliseconds (the header's `created_unix_ms`, the POSITION coalescing, and `open`'s `load_ms`). Document passes its own `DocumentOptions.now_ms` here, so a test that injects a clock into the document also controls the measured load time; `write`, `sync` and `truncate` replace `::write`, `fdatasync`/`fsync` and `ftruncate` on the sidecar descriptor, so tests can count fsyncs and inject a failing or short write; `progress` is the [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink) that `copy_to`, `rewrite` and `flush` report to (empty: nothing is reported), which Document passes on from its `DocumentOptions.progress`. The seams are copied into a `Sidecar` made by `copy_to`.

Callbacks, set once by Document with `set_callbacks`: `on_payload_written(node, op_index, which, SidecarRef)` and `on_failure(message)`, both run on the main thread through the `EventQueue`. Posted closures hold a shared token that the destructor clears, so a closure that runs after the `Sidecar` is gone does nothing.

Accessors: `paused()`, `had_file()` (a sidecar file was there when `open` ran) and `state()` (`pathless`, `attached`, `read_only`, `disabled`, or `session_only` when the sidecar file could not be created, for example in an unwritable directory) and `path()`. `make_session_only(queue)` makes a sidecar that is `session_only` from the start and never writes; Document uses it when the history could not be copied by Save As, or when no base hash can be computed.
- **State changes:** `closed → loading → (attached | read_only | disabled)`, or `pathless` for an untitled document. A `pathless` sidecar has no file and never writes: it behaves as if permanently under the deferral rule, keeping every appended record in memory, in order, until [copy_to](#function-copy_to) gives it a path at the first Save As. Its ROOT needs no wait, because the empty base's hash is a constant. **Deferral rule:** until the base hash is known (verification resolved, or the hash for a new ROOT available), every append is queued in memory and none is written. Node parents may still change on a verification mismatch, and the format has no re-parent record. A crash during that window loses only that session's history, not file content. The file is created lazily on the first `append_node`, so merely viewing a file never creates a `.history`: until a NODE record is queued for a file that does not exist yet, ROOT, SAVE and POSITION records are held back as under the deferral rule, and the first NODE releases them ahead of itself. Invariant: records reach the file in the order the main thread enqueued them, and a NODE record is written only after every PAYLOAD record it references.
- **Owns:** the file handle, the lock, the writer thread, and the payload mapping.
- **Access:** `Document` only. Destruction flushes and joins the writer.
- **Referred by:** [document](./document.hpp.skel.md)
- **Referred by:** [sidecar (implementation)](./sidecar.cpp.skel.md)
- **Referred by:** [sidecar_test](../../tests/sidecar_test.cpp.skel.md)
- **Referred by:** [history_model_test](../../tests/history_model_test.cpp.skel.md)
- **Referred by:** [fuzz_sidecar](../../fuzz/fuzz_sidecar.cpp.skel.md)

### function: open

Loads existing history and decides where the document currently sits in it.

- **Inputs:** `tree`: an empty `UndoTree&` to fill; `file_size`: the size of the document on disk.
- **Returns:** a `Result<LoadOutcome>` where `LoadOutcome` is `{ state: no_history | provisional | read_only | disabled; candidate: optional NodeId; load_ms: int64_t }`.
  - `load_ms` is how long this call took. It is measured with the `now_ms` seam, read on entry and on return, and clamped at 0 in case the clock steps backwards. It covers reading and validating the records, which is the whole of the history load. It does not cover the later hash verification, which belongs to the scanner. [App](../app/app.hpp.skel.md#class-app) offers a prune when it exceeds 5000. It is 0 when no sidecar file exists.
  - `candidate` is the most recent save point (SAVE record, or ROOT record by its base) whose recorded size equals `file_size`. `no_history` means there is no candidate, whether or not the file held history; the loaded trees then stay in the forest and Document starts a new root. `read_only` (lock held elsewhere) may still carry a candidate; Document verifies it as in `provisional` but never appends. `disabled` (foreign file, unreadable header, or an unknown hash algorithm) loads nothing. A newer format version loads nothing and is `read_only`.
  - Records whose CRC is valid but whose content is invalid (a NODE whose parent is unknown) are skipped, and loading continues.
  - Final verification compares the recorded hash with the [LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner) hash; see `verify`.
- **State changes:** reads the whole sidecar sequentially, validates each record (every record's CRC is checked, PAYLOAD bodies included), calls [UndoTree.load_root](./undo_tree.hpp.skel.md#function-load_root), [UndoTree.load_node](./undo_tree.hpp.skel.md#function-load_node), `mark_saved` and `set_position`, takes the lock, and maps the payload area. A truncated or corrupt tail is cut off (`ftruncate` to the end of the last good record) by the writer thread before its first append; where the system refuses to shorten the file while it is mapped (Windows), the good part is written to a new file that replaces it ([write_atomically](../platform/fs.hpp.skel.md#function-write_atomically)), which is then locked in place of the old one, and only if that fails too is the history disabled.
- **Access:** [Document.open](./document.hpp.skel.md#function-open).
- **Failure modes:** a multi-GB sidecar loads slowly: PAYLOAD bodies are not parsed, but every record's CRC is checked, as the format requires, so the whole file is read once. Large payloads stay on disk as `SidecarRef`s.

### function: verify

- **Inputs:** `hash`: the content hash of the document on disk.
- **Returns:** `matched(NodeId)` or `mismatch`.
- **State changes:** none. Document reacts by attaching or by starting a new root.
- **Access:** Document, when the scanner finishes.
- **Referred by:** [document (implementation)](./document.cpp.skel.md)

### function: release_deferred

- **Inputs:** `tree`: the `UndoTree`, read for the final parents.
- **Returns:** nothing.
- **State changes:** hands every queued append, in order, to the writer thread, and ends the deferral. Each held-back NODE takes its parent from `tree` at this point, so a re-rooting is reflected. ROOT records are always placed before every other record held back with them. `defer()` starts holding records back again (Document calls it on reload, whose new ROOT waits for the new scan). `remap_deferred(from, to)` rewrites held-back SAVE and POSITION records that name `from` (the candidate) to name `to` (the new root), for a save made while verifying. `discard_deferred()` drops every held-back record (a reload while verifying abandons the verification, and with it the session's unverified records). A `pathless` sidecar ignores `release_deferred`.
- **Access:** Document, after `verify` has resolved or after `append_root` for a new history.

### function: append_root

- **Inputs:** a root `NodeMeta`, `base_size`, `base_hash`.
- **Returns:** nothing.
- **State changes:** enqueues a ROOT record.
- **Access:** Document.

### function: append_node

- **Inputs:** `meta`; `ops`, with `Pieces` payloads given as worker-safe views from [PieceTree.frozen_bytes](../text/piece_tree.hpp.skel.md#function-frozen_bytes).
- **Returns:** nothing.
- **State changes:** enqueues PAYLOAD records for non-inline payloads, then the NODE record. When the writer finishes, it posts `on_payload_written(node, op_index, which, SidecarRef)` back to the main thread.
- **Access:** Document, when a node is closed, which is after coalescing ends.

### function: append_save

- **Inputs:** `node`, `size`, `hash`.
- **Returns:** nothing.
- **State changes:** enqueues a SAVE record. The writer thread fsyncs the sidecar after writing it; see Durability in the module.
- **Access:** Document.save and Document.save_as.

### function: append_position

- **Inputs:** `current`: a node; `preferred_changes`: a list of `(parent, child)` pairs.
- **Returns:** nothing.
- **State changes:** enqueues a POSITION record. Coalesced to at most one per second.
- **Access:** Document, after undo, redo and cycle_branch. This lets the next session restore where the user was, including after a crash.

### function: payload_bytes

- **Inputs:** `text`: the document's `PieceTree&`; `ref`: a `SidecarRef`.
- **Returns:** `Result<std::pair<BufferIndex, uint64_t>>`: a `(BufferIndex, offset)` usable as a `Piece` in the document's tree; `io` when the file cannot be mapped or is shorter than the reference. The payload mapping is registered with [PieceTree.add_buffer](../text/piece_tree.hpp.skel.md#function-add_buffer) the first time and remapped when the file has grown past the current mapping.
- **State changes:** may add a buffer to the tree. After a [rewrite](#function-rewrite), the next call maps the new file and registers it as a new buffer. Pieces already in the document keep the old mapping alive through its `shared_ptr`, and the replaced inode stays readable through it.
- **Access:** Document, when undo or redo materializes a `SidecarRef`, and [Document.prune_history](./document.hpp.skel.md#function-prune_history) while it computes the anchor's content.

### function: set_paused

Persistence off: while paused, an `attached` sidecar keeps every record it is given in its held queue, in order, instead of writing it, exactly as a `pathless` one does, so no file is created and an existing one is not touched. Unpausing queues the held records for the writer, creating the file (with a fresh header) when it does not exist yet; a deferral in progress still holds its records until `release_deferred`, which while paused fixes their parents but keeps holding them.

- **Inputs:** `paused`.
- **Returns:** nothing.
- **State changes:** as above. Has no effect in `pathless`, `read_only`, `disabled` and `session_only`, which never write anyway.
- **Access:** [Document.set_persist_history](./document.hpp.skel.md#function-set_persist_history) and `open`.

### function: copy_to

Copies the whole history to the sidecar of a new document path, for Save As.

- **Inputs:** `new_doc_path`: the resolved real path of the Save As target; `file_mode`: the new document's permission bits; `tree` and `text`: the document's `UndoTree` and `PieceTree`, read to serialize the nodes of step 4 (`Pieces` payloads through [PieceTree.frozen_bytes](../text/piece_tree.hpp.skel.md#function-frozen_bytes)).
- Durability: after the new sidecar has been written, the writer thread fsyncs it once; the SAVE record that Save As appends next is fsynced as for any save.
- **Returns:** `Result<std::unique_ptr<Sidecar>>`: a `Sidecar` for `sidecar_path_for(new_doc_path)` that holds every record this one holds or has queued, with its lock taken. On error, `this` is unchanged and still usable.
- **State changes:**
  1. Waits for the writer queue to drain, as `flush` does. Records still held back by the deferral rule are **not** forced out.
  2. If `<new>.history` already exists: when it is **not** a sidecar (bad magic, for example Save As `go` next to an existing `go.mod`), it is never touched, and `copy_to` returns `format` so that the new path gets session-only history. When it **is** a valid sidecar (the history of the file that Save As just overwrote) and no other `mod` holds its lock, it is **replaced** by the copy below; that file's history is lost, by decision, because the file it described was overwritten. When its lock is held by another running `mod`, it is not replaced: the new `Sidecar` is returned in `read_only`, as `open` would, because it belongs to a live session.
     If this sidecar's file exists and is valid, copies it byte for byte to the new sidecar path through [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically), passing `file_mode` as its `mode` input (no `mode_from`), so the new sidecar gets exactly the new document's permission bits. A byte-for-byte copy keeps every `SidecarRef` offset valid in the new file, so the [UndoTree](./undo_tree.hpp.skel.md#class-undotree) needs no rebinding. Where the platform offers a kernel copy or clone (`copy_file_range` on Linux, `clonefile` on macOS), the producer may use it.
  3. If there is no file yet (it is created lazily, or this sidecar is `pathless` because the document was untitled), nothing is copied; the new file starts with a fresh header and receives the queued records in step 5.
  4. Records this session's nodes that never reached this sidecar's file, because it was `read_only` (lock held elsewhere) or `disabled` (bad magic, or a write failure), are serialized from the in-memory `UndoTree` and appended to the new file in creation order, after a fresh header if there was nothing to copy. A sidecar whose format version is newer than supported is copied as is, and the new one is opened `read_only`, like the old.
  5. The new `Sidecar` takes over the deferred queue, so a Save As during verification still writes the session's records with their final parents, to the new file only.
- **Access:** [Document.save_as](./document.hpp.skel.md#function-save_as), after the new document file has been written.
- **Depends on:** [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically)
- **Referred by:** [Document.save_as](./document.hpp.skel.md#function-save_as)
- **Depends on:** [sidecar_file](../../infra/storage.iac.skel.md#resource-sidecar_file)
- **Failure modes:** `no_space` or `permission` on the new directory: return the error, so that Document falls back to session-only history for the new path. The lock on the new sidecar is held by another `mod`: return the new `Sidecar` in `read_only`, as `open` would. The copy of a multi-GB sidecar is slow: the producer reports `"copying history"` to the `progress` seam after every slice it writes, with `done` the bytes written so far and `total` the length of the file being copied (`nullopt` when there is no file to copy, or for a newer-version file copied to its end).
- **Depends on:** [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink)
- **Referred by:** [progress_test](../../tests/progress_test.cpp.skel.md)


### function: clear

Deletes this document's sidecar, for Clear History….

- **Inputs:** none.
- **Returns:** `Status`. `unsupported` in `read_only` (the lock is held by another `mod`, or the format version is newer than supported): the file is not ours to delete. An I/O error if the file cannot be deleted.
- **State changes:**
  1. Drops every record still held back by the deferral rule and every record queued for the writer thread but not yet written, and waits for the writer to finish the record in progress. Rebind closures already posted for discarded nodes are ignored when they run; see [UndoTree.rebind_payload](./undo_tree.hpp.skel.md#function-rebind_payload).
  2. In `attached`, or `disabled` after a write failure, deletes the sidecar file (`unlink` while still holding the lock, then closes it, which releases the lock). The payload mapping is not unmapped here: it stays alive for as long as document pieces reference it through their `shared_ptr`.
  3. In `disabled` because the path holds a foreign file (bad magic), touches nothing on disk. In `pathless`, or when no file was ever created, there is nothing to delete.
  4. Returns to the "no file yet" state for the same path: the next append creates a new file with a fresh header and takes the lock again, as for a document that never had history.
- **Access:** [Document.clear_history](./document.hpp.skel.md#function-clear_history) only.
- **Depends on:** [sidecar_file](../../infra/storage.iac.skel.md#resource-sidecar_file)
- **Failure modes:** a crash between the unlink and the next append leaves no sidecar at all, which is the intended outcome. A second `mod` that already had the file open read-only (because this instance held the lock) keeps showing the old history until it closes; that is accepted.

### function: rewrite

Replaces this document's sidecar with the pruned history, for [Document.prune_history](./document.hpp.skel.md#function-prune_history). The file is rewritten smaller, as a normal format-version-1 sidecar that any reader can load.

- **Inputs:** `tree`: the unchanged `UndoTree`, which is read only; `plan`: a [PrunePlan](./undo_tree.hpp.skel.md#symbol-pruneplan) for it; `base_size` and `base_hash`: the anchor's content, which become the ROOT record's base; `text`: the document's `PieceTree`, read through [PieceTree.frozen_bytes](../text/piece_tree.hpp.skel.md#function-frozen_bytes) for `Pieces` payloads.
- **Returns:** `Result<std::vector<Rebind>>`. `Rebind` is a public struct `{ NodeId node; uint32_t op_index; Which which; SidecarRef ref; }`, one for every payload written as a PAYLOAD record, where `op_index` is the op's *pruned* index (see [PrunedNode](./undo_tree.hpp.skel.md#symbol-prunednode)). Possible errors:
  - `unsupported` in any state other than `attached`.
  - `not_atomic` when the sidecar has other hard links, or its directory is not writable.
  - `no_space`, `permission` or `io` from the write.
  - `canceled` when the writer queue does not drain.
  On error, the old file and this object are unchanged, apart from the drained queue.
- **State changes:**
  1. Waits for the writer queue to drain and for its rebind closures to run, as `flush` does. No record can still be held back for an unknown base hash, because Document refuses to prune until that hash is known. Records held back only because no file exists yet are dropped, since the new file contains everything they describe. So is a POSITION record still being coalesced under the one-per-second rule: the rewrite writes its own.
  2. Writes the new file with [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically) to `sidecar_path_for(doc_path)`. It passes the document's permission bits as `mode` and gives no `mode_from`, as `copy_to` does, so copying extended attributes can never make the rename impossible. The content, in this order:
     - A fresh header. Its `created_unix_ms` is the old header's when there was a file, otherwise `now_ms`.
     - One ROOT record for the new root. Its `node_id` is `plan.anchor`, its base is `base_size` and `base_hash`, and its `time_ms` is `plan.root_time_ms`.
     - For each [PrunedNode](./undo_tree.hpp.skel.md#symbol-prunednode) from [UndoTree.for_each_pruned](./undo_tree.hpp.skel.md#function-for_each_pruned), in ascending id: PAYLOAD records for its payloads longer than `kInlineMax`, then its NODE record with the pruned meta and the concatenated ops. Payload bytes come from the inline bytes, from frozen views of `Pieces`, or from the **old** sidecar mapping for a `SidecarRef`. They are copied, never re-referenced, because offsets in the old file mean nothing in the new one. A `SidecarRef` payload is always copied into a PAYLOAD record, whatever its length, so that its in-memory reference can be rebound; inline and `Pieces` payloads follow the usual `kInlineMax` rule. A `SidecarRef` that lies outside the old file gives `io` before anything is written.
     - One SAVE record per kept save point, in the order their SAVE records were originally written ([UndoTree.save_order](./undo_tree.hpp.skel.md#function-root_base)), so the latest save point is still last. The anchor's SAVE record, if it was a save point, names the new root. The tree does not keep the original SAVE times, so these records carry `now_ms` as `time_ms`.
     - One POSITION record: `current`, and the `(parent, preferred_child)` of every node that has one after the prune, as [UndoTree.apply_prune](./undo_tree.hpp.skel.md#function-apply_prune) will set them ([UndoTree.pruned_preferred](./undo_tree.hpp.skel.md#function-pruned_preferred)).

     The producer runs on the main thread and reports `"pruning history"` to the `progress` seam after every slice it writes, with `total` the sum of the kept payload lengths (an estimate: the records' framing adds a little).
  3. After the rename, opens the new file, takes its lock (`flock(LOCK_EX|LOCK_NB)`), then closes the old descriptor, which releases the lock on the replaced inode. The old payload mapping is dropped from this object, but stays alive for as long as document pieces reference it.
  4. The writer thread fsyncs the new file once (a sync job queued ahead of any append, through the `sync` seam). Appends then continue on the new file.
- **Access:** [Document.prune_history](./document.hpp.skel.md#function-prune_history) only.
- **Depends on:** [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically)
- **Depends on:** [UndoTree.for_each_pruned](./undo_tree.hpp.skel.md#function-for_each_pruned)
- **Depends on:** [UndoTree.pruned_preferred](./undo_tree.hpp.skel.md#function-pruned_preferred)
- **Depends on:** [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink)
- **Depends on:** [sidecar_file](../../infra/storage.iac.skel.md#resource-sidecar_file)
- **Failure modes:**
- **Referred by:** [progress_test](../../tests/progress_test.cpp.skel.md)
  - **Lock race.** Between the rename and the new lock, the name points to an unlocked file. If a second `mod` opens it in that instant and takes the lock first, this sidecar becomes `read_only` and stops appending, with the status "history is open in another mod". The rewrite itself has already succeeded, so Document still applies the prune. This is the same window `copy_to` has.
  - **Large payloads.** A history with large kept payloads is copied byte for byte, which is slow.
  - **Old readers.** Another process that had the old file open keeps reading the old inode.
  - **Crash.** A crash before the rename leaves the old sidecar intact, plus an orphaned temp file. A crash after it leaves the pruned sidecar.

### function: detach_views

- **Inputs:** `mapping`: the address of a `MappedFile` that is about to be overwritten in place.
- **Returns:** nothing.
- **State changes:** every record still held back or queued for the writer whose payload view points into `mapping` gets its own heap copy of those bytes, and drops the view. Waits for the record in progress first, so no view of `mapping` is read after the call returns.
- **Access:** [Document.save](./document.hpp.skel.md#function-save) with `in_place`, before the write.
- **Referred by:** [Document.save](./document.hpp.skel.md#function-save)

### function: flush

- **Inputs:** `timeout_ms`.
- **Returns:** `Status`; `canceled` on timeout.
- **State changes:** blocks until the writer queue is empty and the rebind closures have been posted, then runs `EventQueue.drain`. It waits in slices of about 100 ms; when the queue was not empty at the start, it reports `"writing history"` to the `progress` seam after each slice and once at the end, with `done` the bytes the writer has written since the call began and `total` the payload bytes queued at the start (an estimate). An empty queue reports nothing.
- **Depends on:** [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink)
- **Access:** Document.save, before `PieceTree.rebase`, so that old buffers can be released, and at exit.
- **Referred by:** [progress_test](../../tests/progress_test.cpp.skel.md)
