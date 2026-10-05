---
role: product
stamp: source 787ee9d0, stand-in bf3d6b75
---
# module: document

One open file: its piece tree, its undo history, its sidecar and background scan, and its save logic. Every content mutation in the program goes through `Document`, so undo recording, change notification (highlighters, LSP, view) and dirty tracking cannot be bypassed.

- **Owns:** the [PieceTree](../text/piece_tree.hpp.skel.md#class-piecetree), the [UndoTree](./undo_tree.hpp.skel.md#class-undotree), the [Sidecar](./sidecar.hpp.skel.md#class-sidecar), the [LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner), the open coalescing group, the path, the detected line ending, the history state, and the *known disk identity*: the [FileIdentity](../platform/file_map.hpp.skel.md#symbol-fileidentity) that `mod` last read, wrote or was told to keep, against which external changes are detected.
- **Access:** public. Exactly one instance, owned by [App](../app/app.hpp.skel.md#class-app) and replaced on File > Open. Main thread only.

A document is either **named** (it has a path, from `open` or after a successful `save_as`) or **untitled** (from [open_untitled](#function-open_untitled), when `mod` starts with no arguments). An untitled document becomes named on its first successful `save_as`.
- **Required:** always.
- **Failure modes:** listed per function. Optional subsystems such as the sidecar and the scanner never make `open` or `save` fail.
- **Depends on:** [PieceTree](../text/piece_tree.hpp.skel.md#class-piecetree)
- **Depends on:** [UndoTree](./undo_tree.hpp.skel.md#class-undotree)
- **Depends on:** [Sidecar](./sidecar.hpp.skel.md#class-sidecar)
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Unknowns:** none

## symbol: ChangeEvent

`{ uint64_t offset; uint64_t removed_len; uint64_t inserted_len; std::optional<std::string_view> inserted_small; uint64_t version; }`.

- `inserted_small` is present only when the inserted text is at most 64 KiB.
- `version` increases by 1 per primitive op.

Listeners receive `before_change(ev)` before the tree mutates, so they can compute pre-edit line and column positions, and `after_change(ev)` after it.

- **Access:** public.

## symbol: DocumentListener

An interface with `before_change`, `after_change`, `reloaded` (after open, rebase or [reload](#function-reload)) and `saved` (after `save` and `save_as`).

- **Access:** implemented by [EditorView](../ui/editor_view.hpp.skel.md#class-editorview), [MarkdownHighlighter](../syntax/markdown.hpp.skel.md#class-markdownhighlighter), [SemanticHighlighter](../syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter) and [Editor](./editor.hpp.skel.md#class-editor), which adjusts the cursor when an edit happens elsewhere, as with replace-all.
- **Referred by:** [highlight](../syntax/highlight.hpp.skel.md)

## symbol: HistoryState

`enum { session_only, verifying, attached, read_only, disabled }`. Shown on the status line. An untitled document is `session_only` until its first `save_as`. After `reload`, the state is unchanged except that `verifying` becomes `attached` (with the ROOT deferred) or stays `session_only`. Undo past the session's first node is refused while `verifying`.

- **Access:** public.

## class: Document

- **Inputs:** construction goes through `open` or `open_untitled`. `queue`: an `EventQueue&`. `options`: a `DocumentOptions { now_ms; chunk_size; progress; history; persist_history; }`, defaulted (`persist_history` is `PersistHistory::if_present`, the rule of [set_persist_history](#function-set_persist_history); `always` starts with persistence on, as tests of the written history do), so tests can inject a clock and small chunks: `now_ms` is a `std::function<int64_t()>` returning Unix time in milliseconds (default: the system clock), used for node times, the coalescing timeout, the prune cutoff, and, passed on as the [Sidecar](./sidecar.hpp.skel.md#class-sidecar)'s `now_ms` seam, the measured history load time; `chunk_size` is passed to the [PieceTree](../text/piece_tree.hpp.skel.md#class-piecetree) (default 1 MiB). `progress` is a [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink) (default empty), used by `prune_history` and passed on as the Sidecar's `progress` seam, so App's status-line meter sees every long synchronous operation the document runs. `history` (default true): when false, `open` never reads or writes the file's sidecar; the history lives for the session only, as with a session-only fallback but with no status message. Read-only mode opens link targets this way.
- **State changes:** invariants:
  - The tree content equals the base content at the root of `UndoTree.current`'s tree, with the path's ops applied.
  - `version` increases monotonically.
  - There is at most one open coalescing group, and it is always the `current` node.
- **Owns:** see the module.
- **Access:** App and Editor.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [document (implementation)](./document.cpp.skel.md)
- **Referred by:** [editor](./editor.hpp.skel.md)
- **Referred by:** [undo_tree_test](../../tests/undo_tree_test.cpp.skel.md)
- **Referred by:** [markdown_test](../../tests/markdown_test.cpp.skel.md)
- **Referred by:** [lsp_client_test](../../tests/lsp_client_test.cpp.skel.md)
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)
- **Referred by:** [document_slot](../app/document_slot.hpp.skel.md)
- **Referred by:** [history_model_test](../../tests/history_model_test.cpp.skel.md)
- **Referred by:** [fuzz_editor](../../fuzz/fuzz_editor.cpp.skel.md)
- **Referred by:** [fuzz_sidecar](../../fuzz/fuzz_sidecar.cpp.skel.md)

### function: open

- **Inputs:** `path`; `queue`; `options`. Ambient: the filesystem.
- **Returns:** `Result<std::unique_ptr<Document>>`. A path that does not exist gives an empty, new document whose save creates the file. Permission or other I/O failures return an error.
- **State changes:**
  1. Resolves the real path.
  2. Maps the file and seeds the tree.
  3. Detects the line ending from the first line feed (CRLF or LF), looking no further than the first chunk, so that a file without line feeds costs no extra I/O; LF when none is found.
  4. Starts the LineScanner.
  5. Opens the sidecar. In every outcome, `UndoTree.current` is valid before `open` returns, so editing can start at once:
     - If there is a provisional match, `current` = candidate and the state is `verifying`.
     - If there is no history, an in-memory root is created immediately. Its ROOT *record* is written once the scan hash arrives; see the deferral rule in [Sidecar](./sidecar.hpp.skel.md#class-sidecar).
     - If the sidecar is unavailable, the state is `session_only` and an in-memory root is created.
- **Access:** App startup and File > Open.
- **Depends on:** [MappedFile.open](../platform/file_map.hpp.skel.md#function-open)
- **Depends on:** [resolve_real_path](../platform/fs.hpp.skel.md#function-resolve_real_path)
- **Depends on:** [is_writable](../platform/fs.hpp.skel.md#function-is_writable)
- **Depends on:** [LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner)
- **Depends on:** [target_file](../../infra/storage.iac.skel.md#resource-target_file)
- **Failure modes:** a read-only file opens normally and marks the document `read_only_file`; save reports `permission`. A directory path gives `unsupported`.

### function: open_untitled

Creates the untitled buffer that `mod` opens when started with no arguments, for starting a new file.

- **Inputs:** `queue`; `options`.
- **Returns:** `std::unique_ptr<Document>`. It cannot fail.
- **State changes:** creates an empty piece tree with no original buffer and no mapping, no path, line ending LF, and no LineScanner (there is nothing to scan). Creates an in-memory root with `base_size` 0 and `base_hash` = the SHA-256 of empty input, a known constant, so the ROOT needs no scan and no deferral for its hash. Creates a *pathless* [Sidecar](./sidecar.hpp.skel.md#class-sidecar) that keeps every record in memory until the first `save_as` gives it a path. The history state is `session_only`. The known disk identity is empty, so `check_external_change` always returns `unchanged`.
- **Access:** App startup, from [main](../main.cpp.skel.md#function-main) with no path. File > Open never produces an untitled document.

  Behavior of the untitled document elsewhere: `save` is never called on it (App turns Save into the Save As prompt; a direct call returns `unsupported`); `is_dirty` is true once it has content, so an empty untitled buffer exits without a prompt; the status line shows `[untitled]` in place of the path; no highlighter is chosen until `save_as` gives it an extension.

### function: apply

The single mutation primitive.

- **Inputs:** `offset`; `remove_len`; `insert`: bytes, a `PieceRun` or a `SidecarRef`; `kind`: an `EditKind`; `cursor_before` and `cursor_after`: for restoring the cursor on undo and redo.
- **Returns:** nothing.
- **State changes:**
  1. Notifies `before_change`.
  2. Erases (capturing a `PieceRun`) and inserts into the tree.
  3. Bumps the version.
  4. Records the op: it is amended into the open group if it coalesces, or a new node is committed. The previous group is closed and handed to [Sidecar.append_node](./sidecar.hpp.skel.md#function-append_node).
  5. Notifies `after_change`.

  Coalescing rule: a `typing` op coalesces with an open `typing` group when it is adjacent to that group's last op and less than the coalescing timeout, `kCoalesceTimeout` = 1 s, has passed since that op. A pause of 1 s or more starts a new node. The same holds for `delete` with `delete`. Whitespace after non-whitespace starts a new group, so undo works word by word. Every other kind always closes the group. The whitespace rule applies to `typing` only: deletions coalesce regardless of what they delete.

  Payload forms: removed or inserted content of at most `kInlineMax` bytes is recorded `Inline` (read from the tree before it is erased); larger content is recorded as `Pieces`, the `PieceRun` returned by `erase` or covering the inserted range ([PieceTree.pieces](../text/piece_tree.hpp.skel.md#function-pieces)). So a `Pieces` payload is always longer than `kInlineMax`, and every one becomes a PAYLOAD record. Inserted bytes are given as a `std::string_view`; an inserted `SidecarRef` is materialized through [Sidecar.payload_bytes](./sidecar.hpp.skel.md#function-payload_bytes), with its line feeds counted when it is inserted.
- **Access:** [Editor](./editor.hpp.skel.md#class-editor) and [Searcher](../search/search.hpp.skel.md#class-searcher).
- **Referred by:** [search](../search/search.hpp.skel.md)

### function: begin_group

- **Inputs:** `kind`.
- **Returns:** nothing.
- **State changes:** closes any open group and opens a group that every `apply` joins until `end_group`. Depth-counted.
- **Access:** Searcher.replace_all, paste over a selection, and Editor operations that delete and insert together.

### function: end_group

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** at depth 0, closes the group. An empty group is dropped.
- **Access:** paired with `begin_group` (use RAII via a `GroupGuard`).

### function: undo

- **Inputs:** none.
- **Returns:** `Result<uint64_t>`: the cursor offset to restore. It returns `canceled` at a root, and `unsupported` when the node predates verification while `verifying`.
- **State changes:** closes the open group, then inverts the node's ops in reverse order through the same before/after notifications as `apply`, without recording them as new nodes. Moves `current`, and enqueues a sidecar POSITION.
- **Access:** Editor (Ctrl+Z, Edit > Undo).

### function: redo

- **Inputs:** none.
- **Returns:** `Result<uint64_t>`: the cursor offset.
- **State changes:** reapplies the preferred child's ops, as in `undo`.
- **Access:** Editor (Ctrl+Y, Edit > Redo).

### function: cycle_branch

- **Inputs:** `direction`.
- **Returns:** a branch indicator for the status line, or `nullopt`.
- **State changes:** see [UndoTree.cycle_branch](./undo_tree.hpp.skel.md#function-cycle_branch). Enqueues a POSITION record.
- **Access:** App commands `NextBranch` and `PrevBranch`.

### function: jump_to

Makes any node of the current root's tree the current state, so that editing continues from there. This is how the undo-history panel "continues from a chosen point".

- **Inputs:** `target`: a `NodeId` chosen in [HistoryView](../ui/history_view.hpp.skel.md#class-historyview).
- **Returns:** `Result<uint64_t>`: the cursor offset to restore, which is `target`'s `cursor_after` (for a root, 0). Errors: `unsupported` when `target` is in a different root's tree (its base content is not available), or when the path passes through nodes that predate verification while the state is `verifying`. Nothing changes on error.
- **State changes:**
  1. Closes the open group.
  2. Computes [UndoTree.path](./undo_tree.hpp.skel.md#function-path) from `current` to `target`.
  3. For each step: an `undo` step inverts that node's ops exactly as `undo` does; a `redo` step applies them exactly as `redo` does. Both go through the same before/after notifications, and neither records a new node. [UndoTree.follow](./undo_tree.hpp.skel.md#function-follow) moves `current` one edge per step and sets preferred children, so a later redo from any node on the way retraces the jump.
  4. Enqueues **one** sidecar POSITION record at the end, carrying `target` and every changed preferred child, not one per step.
  5. After the jump, the next `apply` commits a new child of `target`; the branch the user left is kept.
- **Access:** App, from the history panel's Enter.
- **Depends on:** [UndoTree.follow](./undo_tree.hpp.skel.md#function-follow)
- **Failure modes:** a long path runs synchronously and blocks the UI for its duration; see [HistoryView](../ui/history_view.hpp.skel.md#class-historyview). A failure while applying a step (a `SidecarRef` that cannot be read because the sidecar became unreadable) stops the jump at the last good node, leaves `current` there, still enqueues the POSITION record for the steps taken, and returns the error.
- **Referred by:** [history_view](../ui/history_view.hpp.skel.md)
- **Referred by:** [history_view_test](../../tests/history_view_test.cpp.skel.md)

### function: clear_history

Clear History… in the Undo History pane: deletes the sidecar and starts a new history whose root is the document's present content. Used to purge deleted text, such as secrets, that the history would otherwise keep for ever.

- **Inputs:** none. Precondition: the document is **clean** (`is_dirty` is false). App guarantees it: on a dirty document, Clear History first offers Save / Cancel and calls this function only after a successful save; see [App.run_command](../app/app.hpp.skel.md#function-run_command). App asks for confirmation before calling it.
- **Returns:** `Status`. `internal` (a programming error) when called on a dirty document; it is returned, not asserted, so that the case stays testable in debug builds. `unsupported` when the history state is `read_only`: the lock is held by another `mod`, or the sidecar's format version is newer than supported, so the file belongs to someone else's session or a newer writer and is not deleted. Nothing changes on error.
- **State changes:**
  1. Closes the open group.
  2. Abandons any pending verification: the old history it would have attached to is being deleted. If the scanner has not yet produced the hash of the file on disk, it keeps running, and its hash, when it arrives, becomes the new root's base hash instead of being compared with a save point. Line counts are unaffected.
  3. Calls [Sidecar.clear](./sidecar.hpp.skel.md#function-clear), which deletes this document's sidecar file. Sidecars that Save As copied next to other names are not touched.
  4. Calls [UndoTree.reset](./undo_tree.hpp.skel.md#function-reset), then `add_root` with the present size and the present content's hash, and makes the new root `current`. Because the document is clean, the present content is the file on disk, so its hash is the one recorded by the last save or produced by the scanner (step 2). The root is also marked saved, so the document stays clean. Undo and redo cannot go past the new root. The old history's ops and payloads are gone from memory as well as from disk; only its text-free metadata stays, retired, so the undo-history panel can show it dimmed until the session ends.
  5. The new root's ROOT record follows the deferral rule; the sidecar file is created again lazily, on the next append, so clearing the history of a file that is then only viewed leaves no `.mod` behind.
  6. The history state becomes `attached` (or stays `session_only` for an untitled document or one whose sidecar was unavailable). For a `disabled` sidecar whose path holds a foreign file (bad magic), only the in-memory history is cleared and the foreign file is never touched.
- **Access:** App, after the user confirms Clear History…, and only on a clean document.
- **Depends on:** [Sidecar.clear](./sidecar.hpp.skel.md#function-clear)
- **Depends on:** [UndoTree.reset](./undo_tree.hpp.skel.md#function-reset)
- **Failure modes:** deleting the file fails (permission lost, read-only filesystem): return the error after the in-memory history has **not** been cleared, so the two never disagree. Pieces in the document that came from sidecar payloads (inserted by an earlier undo or redo) keep the old payload mapping alive through its `shared_ptr`; on POSIX the deleted file stays readable through the mapping until the last such piece is gone, so the document content is unaffected.


### function: prune_preview

Counts what a prune would remove, so App can show the age prompt and the confirmation.

- **Inputs:** `days`: a whole number of days, 0 or more, or none to get only the oldest age. Ambient: `now_ms`.
- **Returns:** `Result<PrunePreview>`. `PrunePreview` is `{ std::optional<uint64_t> oldest_days; int64_t cutoff_ms; uint64_t remove_count; uint64_t keep_count; uint64_t removed_trees; }`.
  - `oldest_days` is `floor((now − UndoTree.oldest_change()) / 86 400 000)`, or `nullopt` when the current tree has no changes.
  - `cutoff_ms` is `now − days × 86 400 000`.
  - `remove_count` is the plan's `removed_count`, `keep_count` its number of kept nodes, and `removed_trees` its count of other live trees. All three are 0 when `days` is none. The other trees go whatever their age: they cannot be reached from the current tree, because their base content is not available.
  - Errors, all `unsupported`, carry the message that App shows:
    - "history is still being verified": the state is `verifying`, or the current root's base hash is not known yet.
    - "history is open in another mod": `read_only`.
    - "history is not being saved": `session_only` or `disabled`. That covers an untitled document, a sidecar that could not be created, and a foreign `.mod` file.
- **State changes:** closes the open group, because the plan must see it as a node. Nothing else.
- **Access:** App, for the prune prompts.
- **Depends on:** [UndoTree.plan_prune](./undo_tree.hpp.skel.md#function-plan_prune)
- **Depends on:** [UndoTree.oldest_change](./undo_tree.hpp.skel.md#function-oldest_change)

### function: prune_history

Deletes the history older than a cutoff, for the Undo History pane's Trim History… and for the slow-load offer. The user's rule is in [UndoTree.plan_prune](./undo_tree.hpp.skel.md#function-plan_prune). The step the document is at and its latest save point are always kept. Every kept step stays reachable from the new root, so jumping between the kept branches still works. The document's content, cursor and dirty state do not change.

- **Inputs:** `cutoff_ms`, from the `PrunePreview` the user confirmed.
- **Returns:** `Status`. It has the same `unsupported` cases as `prune_preview`, plus any error from [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite) (`not_atomic`, `no_space`, `permission`, `io`, `canceled`) or from reading a payload while computing the anchor's content. **Nothing changes on error:** the tree and the sidecar file are pruned together, or neither is.
- **State changes:**
  1. Closes the open group and computes the plan with `plan_prune(cutoff_ms)`. A plan that removes nothing returns success with no other change.
  2. **Anchor content.** If the anchor is the existing root, its recorded base is used. If the anchor is a live save point, its recorded size and hash are used. Otherwise the content is computed with a *silent walk*:
     - Follow [UndoTree.path](./undo_tree.hpp.skel.md#function-path) from `current` to the anchor, applying each step to the piece tree only. There are no listener notifications, no version bump and no `follow`, so neither `current` nor any preferred child moves.
     - Each step's erased `PieceRun` is kept, and `SidecarRef` payloads are materialized through the **old** sidecar mapping with [Sidecar.payload_bytes](./sidecar.hpp.skel.md#function-payload_bytes).
     - At the anchor, stream [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read) through a [ContentHasher](../util/hash.hpp.skel.md#class-contenthasher) to get the size and hash.
     - Walk back in reverse: erase what each step inserted, and reinsert its kept run with [PieceTree.insert_run](../text/piece_tree.hpp.skel.md#function-insert_run). Reinsertion reads no payload, so the walk back cannot fail.

     If a forward step fails, the steps already taken are walked back the same way and the error is returned. This step must run before step 3, because the rewrite replaces the mapping that `SidecarRef`s point into.
  3. Calls [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite) with the unchanged tree, the plan and the anchor's content.
  4. On success, calls [UndoTree.apply_prune](./undo_tree.hpp.skel.md#function-apply_prune), then [UndoTree.rebind_payload](./undo_tree.hpp.skel.md#function-rebind_payload) for every `Rebind` returned. Nothing is appended, because the rewritten file already holds the position.
  5. Listeners are not notified: the text did not change. App closes the [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) if it is open, and its next `open` shows only the pruned tree, with nothing dimmed for what was removed.
- **Access:** App, after the user has entered an age and confirmed the counts.
- **Depends on:** [UndoTree.plan_prune](./undo_tree.hpp.skel.md#function-plan_prune)
- **Depends on:** [UndoTree.apply_prune](./undo_tree.hpp.skel.md#function-apply_prune)
- **Depends on:** [Sidecar.rewrite](./sidecar.hpp.skel.md#function-rewrite)
- **Depends on:** [ContentHasher](../util/hash.hpp.skel.md#class-contenthasher)
- **Depends on:** [ProgressSink](../util/progress.hpp.skel.md#symbol-progresssink)
- **Failure modes:**
- **Referred by:** [progress_test](../../tests/progress_test.cpp.skel.md)
  - **Hash cost.** When the anchor is neither the root nor a save point, its hash costs a full read of the document, plus a walk the length of the path from `current` to the anchor. Both are synchronous on the main thread. For a multi-GB file that takes as long as hashing the file. The hash read reports `"hashing"` to `DocumentOptions.progress` after every span it reads, with `total` the document size; the rewrite then reports its own progress through the sidecar.
  - **Rewrite cost.** The rewrite copies every kept payload.
  - **Retained text.** Old text is kept inside the tops' absorbed first steps, which the user accepted.
  - **Lost save points.** SAVE records of deleted nodes are gone. If the file on disk is later reverted, outside `mod`, to content that only a deleted save point matched, the next open finds no candidate and starts a new root.

### function: save

Writes the document to its path.

- **Inputs:** `mode`: `atomic` (the default) or `in_place`. `in_place` is passed only after the user has confirmed it, in the flow described in [App.save_document](../app/app.hpp.skel.md#function-save_document). `clipboard`: an optional `Clipboard*`, App's clipboard, used only with `in_place` (see the materialization step); `nullptr` when there is none.
- **Returns:** `Status`. `ErrorCode::not_atomic` means atomic replacement is impossible for this target; nothing was written, the document is unchanged, and the caller asks the user whether to retry with `in_place`.
- **State changes:**
  1. Closes the group.
  2. With `atomic`, calls [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically). With `in_place`, first flushes the sidecar (step 3, brought forward) so that `Pieces` payloads become `SidecarRef`s before any byte of the mapped file changes, then **materializes every in-session reference into the mapping that is about to be overwritten** (below), then calls [write_in_place](../platform/fs.hpp.skel.md#function-write_in_place), which stages the whole new content in a temporary file under `$TMPDIR` and only then copies it over the target, so the producer always reads the original mapping intact. Its producer streams `PieceTree.read` over the whole document, feeding a `ContentHasher` and recording per-chunk line-feed counts at the same chunk boundaries the tree uses.
     **Materialization before an in-place write.** The mappings at risk are every original buffer this document mapped from the target's inode (same device and inode as the file about to be written); older mappings of replaced inodes are safe. Before the first byte of the target changes:
     - every `Pieces` undo payload that still refers to such a mapping (the sidecar flush rebinds payloads only in `attached`, and never those held back by the deferral rule) is copied into the add buffer with [PieceTree.store](../text/piece_tree.hpp.skel.md#function-store) and replaced in the [UndoTree](./undo_tree.hpp.skel.md#function-visit_pieces);
     - every record still queued in the sidecar whose payload view points into such a mapping gets its own copy of the bytes ([Sidecar.detach_views](./sidecar.hpp.skel.md#function-detach_views));
     - the [Clipboard](./clipboard.hpp.skel.md#class-clipboard) content, when it came from this document and refers to such a mapping, is copied into the add buffer and rebound; clipboard content from another document is copied whole into this document's add buffer, because it may map the same inode;
     - a scanner still reading that file is retired first (joined, and its posted line counts ignored by generation), because the write changes its bytes and may shrink the file, and reading a mapping past the end of its file faults. If the base hash of the file on disk is still pending, it is computed synchronously from the old mapping before the write and fed to the same handler the scanner would have called.
     - the document's own text: every piece of the live tree in such a mapping is copied into the add buffer (last to first, each erased and the stored run inserted in its place, the text unchanged), so that if the copy over the file fails partway, or the file cannot be mapped again afterwards, the document still reads its own text, not a mix of old and new bytes;
     The memory cost is up to the total length of those runs, which for the text can be the whole file; that is the price of keeping the text, undo and paste correct, and it is paid only on the in-place path the user confirmed.
  3. Flushes the sidecar so that `Pieces` payloads are rebound to `SidecarRef`s.
  4. Remaps the new file and calls `PieceTree.rebase`.
  5. Stops using the old scanner's line counts: its results for the old buffer are discarded by generation number, and no rescan is needed. If it has not yet produced the *base* hash (states `verifying`, or no-history with the ROOT record still pending), it is **not** canceled. It keeps hashing the old mapping, which its `shared_ptr` keeps alive (the replaced inode stays readable), so verification and the ROOT record still complete.
  6. Calls `UndoTree.mark_saved` and `Sidecar.append_save`, and notifies listeners with `saved`.
  7. Sets the known disk identity to the new file's identity, so that `mod`'s own write is never reported as an external change. Clears the `kept_external` flag (see `keep_in_memory`).
- **Access:** [App.save_document](../app/app.hpp.skel.md#function-save_document) (Ctrl+S, File > Save, and the save branch of the exit and open dirty checks).
- **Depends on:** [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically)
- **Depends on:** [ContentHasher](../util/hash.hpp.skel.md#class-contenthasher)
- **Depends on:** [write_in_place](../platform/fs.hpp.skel.md#function-write_in_place)
- **Depends on:** [PieceTree.store](../text/piece_tree.hpp.skel.md#function-store)
- **Depends on:** [Sidecar.detach_views](./sidecar.hpp.skel.md#function-detach_views)
- **Depends on:** [Clipboard.rebind](./clipboard.hpp.skel.md#function-rebind)
- **Depends on:** [target_file](../../infra/storage.iac.skel.md#resource-target_file)
- **Failure modes:** `no_space` or `permission` from an atomic save leaves the file and the tree untouched and reports on the status line. A failure part-way through an `in_place` save leaves the file corrupt on disk; the document in memory and its history are untouched, so saving again (or Save As elsewhere) recovers it. The status line says so explicitly. If the sidecar flush times out, the save still succeeds, and the old mapping is kept alive by the shared_ptrs in its payloads.


### function: save_as

Writes the document to a new path and moves the document to it. When history is persisted, it goes with it as a copy: the new file gets its own sidecar holding the whole history, and the old sidecar stays next to the old file, unchanged. When it is not, no sidecar is created for the new path: the current (paused or pathless) sidecar is kept, holding the session's records, until [set_persist_history](#function-set_persist_history) turns persistence on.

- **Inputs:** `path`: the new path, not yet resolved; `mode`: `atomic` or `in_place`, as in `save`; `clipboard`: as in `save`. `in_place` is possible only when `path` already exists.

On an untitled document, `save_as` is the first save: step 1 resolves the path against the current working directory, step 3's `copy_to` runs on the pathless sidecar (there is no file to copy, so the whole in-memory history is written to the new sidecar), and step 6 turns the document into a named one.
- **Returns:** `Status`, with the same meaning as `save`, including `not_atomic`. A failure to copy the history is not an error of `save_as`; see the state changes.
- **State changes:**
  1. Resolves the real path. If it is the document's current real path, this is a plain `save`.
  2. Writes the content exactly as `save` steps 1–2 do, but to the new path. On failure, nothing else changes: the document keeps its old path and old sidecar.
  3. Calls [Sidecar.copy_to](./sidecar.hpp.skel.md#function-copy_to) for the new path, passing the new file's permission bits. The old `Sidecar` is closed after the copy (its lock is released, and its file is never written again in this session). The returned `Sidecar` replaces it.
  4. If the copy fails, the save still stands: the document uses a fresh `Sidecar` for the new path in the `session_only` state (in-memory history is kept in full) and the status line says "history not copied: <reason>".
  5. Continues with `save` steps 3–7 against the new file and the new `Sidecar`. The SAVE record goes only to the new sidecar, because the old file was not written.
  6. Switches the document's path, recomputes the sidecar path, and notifies listeners with `saved`. The highlighter is re-chosen by App, because the extension may have changed.
- **Access:** [App.save_document](../app/app.hpp.skel.md#function-save_document) (File > Save As).
- **Depends on:** [Sidecar.copy_to](./sidecar.hpp.skel.md#function-copy_to)
- **Depends on:** [resolve_real_path](../platform/fs.hpp.skel.md#function-resolve_real_path)
- **Failure modes:** the new path is the old file under another name (a hard link or another symlink to it): the real-path check catches symlinks, and the identity check (same device and inode) catches hard links; both become a plain `save`. The new directory is not writable: the content write fails with `permission` and nothing changes. The sidecar copy can be slow for a large history; [Sidecar.copy_to](./sidecar.hpp.skel.md#function-copy_to) reports its progress to the `progress` seam, so App's meter shows it.

### function: check_external_change

Detects that something other than `mod` changed the file. The response is made in [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change) and depends on the kind of change: after a **replacement** the user chooses **reload** (see `reload`), which starts a new history root, or **keep** the in-memory version (see `keep_in_memory`); after an **in-place modification** Reload is the only choice; after a **deletion** there is no prompt and the document is kept (see below). Nothing is ever reloaded without the user choosing Reload, even when the document is clean.

- **Inputs:** none. Ambient: the filesystem.
- **Returns:** `unchanged`, `modified` or `deleted`, comparing [stat_path](../platform/fs.hpp.skel.md#function-stat_path) of the document's real path with the known disk identity. `modified` means any field differs: size, mtime, device or inode. `modified` also carries `replaced`: true when the device or inode differ (the other program wrote a new file and renamed it over this one, which leaves `mod`'s mapping intact), false when only size or mtime differ (it wrote into the same file, so the mapped bytes have changed under the piece tree). A new file that was never saved is always `unchanged`.
- **State changes:** none.
- **Access:** App, every 2 s while idle, and at the start of every save.
- **Depends on:** [stat_path](../platform/fs.hpp.skel.md#function-stat_path)

#### Deleted on disk

When the result is `deleted`, there is no prompt (Reload is impossible). App shows "<file> deleted on disk" on the status line and calls [keep_in_memory](#function-keep_in_memory), so the document behaves as kept: it stays fully editable, `is_dirty` becomes true, and the known disk identity becomes *absent*, so the deletion is reported once, not every 2 s. The piece tree keeps reading the deleted inode through its mapping, which stays valid until it is unmapped. The next save recreates the file through [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically) as for a new file (with that function's new-file permission bits, because there is no file left to copy them from) and appends its SAVE record to the existing sidecar, if there is one. If a file appears at the path again before that save, its identity differs from *absent*, so it is reported as `modified` with `replaced` true and the normal Reload / Keep prompt follows.

### function: reload

Discards the in-memory document state and reopens the file from disk, starting a new history root.

- **Inputs:** none.
- **Returns:** `Status`. On failure (the file vanished between the prompt and the reload, or permission was lost), the document is unchanged and the error is reported.
- **State changes:**
  1. Closes the group.
  2. Maps the file again and replaces the piece tree's content with one piece per chunk, exactly as `open` does. The old mapping stays alive only as long as undo payloads refer to it.
  3. Re-detects the line ending.
  4. Retires the old scanner by generation number and starts a new [LineScanner](../text/line_scanner.hpp.skel.md#class-linescanner) on the new mapping.
  5. Calls [UndoTree.add_root](./undo_tree.hpp.skel.md#function-add_root) with the new size, and makes the new root `current`. No match against existing save points is attempted, even if the new content equals an earlier save. The old trees stay in the sidecar and in memory, but undo cannot reach them from the new root. Unsaved edits made before the reload are therefore not reachable in this session; they remain in the sidecar.
  6. The ROOT record follows the deferral rule in [Sidecar](./sidecar.hpp.skel.md#class-sidecar): it and any later records are written once the new scan produces the base hash.
  7. Sets the known disk identity to the new mapping's identity, clears `kept_external`, and notifies listeners with `reloaded`. [Editor](./editor.hpp.skel.md#class-editor) clamps its cursor to the new size and clears the selection.
- **Access:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change), only after the user chose Reload.
- **Depends on:** [UndoTree.add_root](./undo_tree.hpp.skel.md#function-add_root)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Depends on:** [MappedFile.open](../platform/file_map.hpp.skel.md#function-open)
- **Failure modes:** a reload while the previous scan is still verifying history: that scan's result is discarded by generation number, and the verification is abandoned, because the new root replaces it.

### function: keep_in_memory

Records that the user chose to keep the in-memory version after an external change.

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** sets the known disk identity to the identity now on disk, or to *absent* after a deletion, so the same change is not reported again (a further change is). Sets `kept_external`, which makes `is_dirty` true until the next successful save, because disk and memory now differ. The next save overwrites the other program's change; that is what the user chose.
- **Access:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change), after the user chose Keep for a replaced file, or directly (no prompt) when the file was deleted.
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)

Keep is offered only when the other program **replaced** the file (`modified` with `replaced` true) and after a deletion (`deleted`, with no prompt). When the file was modified **in place** (`replaced` false), the piece tree's unedited ranges point into the mapping, which now shows the other program's bytes, so the original in-memory version no longer exists to keep; App then offers Reload only, and this function is never called for that change.

### function: is_dirty

- **Inputs:** none.
- **Returns:** `!UndoTree.is_at_saved()`, or true for a new file with content, or true while `kept_external` is set.
- **State changes:** none.
- **Access:** status line, quit and open confirmation.

### function: text

- **Inputs:** none.
- **Returns:** `const PieceTree&` for reading.
- **State changes:** none.
- **Access:** views, highlighters, search, LSP and clipboard.

### function: line_of

- **Inputs:** `offset`; `force`.
- **Returns:** the 0-based line number of `offset`, as [PieceTree.line_of](../text/piece_tree.hpp.skel.md#function-line_of) returns it.
- **State changes:** none to the content. Forwarded to the piece tree, whose line queries are non-const because they cache counts they scan; `text()` is const, so this is how readers that need line numbers reach them.
- **Access:** [EditorView](../ui/editor_view.hpp.skel.md#class-editorview) (`force` false, for the gutter and status line).
- **Referred by:** [EditorView](../ui/editor_view.hpp.skel.md#class-editorview)

### function: line_start

- **Inputs:** `line`: 0-based; `force`.
- **Returns:** as [PieceTree.line_start](../text/piece_tree.hpp.skel.md#function-line_start).
- **State changes:** none to the content; caches counts, as `line_of`.
- **Access:** [App](../app/app.hpp.skel.md#function-run_command), for Go to Line (`force` true).

### function: add_listener

- **Inputs:** `listener`: a `DocumentListener*` that outlives its registration.
- **Returns:** nothing.
- **State changes:** registers it; listeners are notified in registration order. `remove_listener` unregisters it, and may be called from the listener's destructor.
- **Access:** Editor, EditorView and the highlighters, on construction.

### function: history

- **Inputs:** none.
- **Returns:** `const UndoTree&`, for [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) and tests.
- **State changes:** none.
- **Access:** read-only, main thread.

### function: begin_preview

Shows the text as it is at a history node, for the Undo History pane, **without changing the document's history**: the text is walked to that node silently (no listeners, no version change, no history change), the way a trim measures a node's content, and stays that way until `end_preview` walks it back exactly. The selected node's own change is then marked: the text it inserted, and the text it removed, which is spliced back in at its place so it can be shown struck through. While a preview is active nothing else may change the document; App keeps the pane modal and ends the preview before any command.

- **Inputs:** `node`: a live node (one that `jump_to` could reach).
- **Returns:** `Result<std::vector<PreviewMark>>`, `PreviewMark { std::uint64_t start, end; bool removed; }` in offsets of the previewed text, sorted. An error (a payload that cannot be read) leaves no preview active.
- **State changes:** ends any earlier preview first. `end_preview()` restores the text byte for byte; `previewing()` says whether one is active.
- **Access:** App, while the Undo History pane is open.
- **Referred by:** [history_preview](../app/history_preview.hpp.skel.md)

### function: set_persist_history

**History is kept in memory by default.** A file is opened with its sidecar *paused* ([Sidecar.set_paused](./sidecar.hpp.skel.md#function-set_paused)), so editing never creates a `.mod` file, unless a sidecar file was there and could be loaded, in which case persistence is on and it keeps being written. `persist_history()` reports which. `history_unreadable()` is true when a sidecar file was there but could not be loaded (its header is not a sidecar's, or `open` failed); persistence is then off. A sidecar locked by another `mod` or written by a newer one is loaded read-only: persistence shows as on but nothing is written, and this function refuses to change it.

- **Inputs:** `on`; `overwrite`: permission to replace an unreadable sidecar file.
- **Returns:** `Status`: `format` when `on` meets an unreadable sidecar and `overwrite` is false (App asks, then calls again with `overwrite` true); `unsupported` in `read_only`; an I/O error from writing the sidecar, with nothing changed.
- **State changes:**
  - `on`, untitled: remembered; the first `save_as` then copies the whole history to the new sidecar as it always has.
  - `on`, with this file's own paused sidecar: unpauses it, which writes every held record, so **the whole session's history** reaches the file.
  - `on`, otherwise (a session-only sidecar, an unreadable file, or a paused sidecar left at the document's previous path by a `save_as` made while persistence was off): with `overwrite`, the unreadable file is deleted first; then [copy_to](./sidecar.hpp.skel.md#function-copy_to) the document's path writes the whole history to a new sidecar, which replaces the current one.
  - off: pauses the sidecar. Writing stops; the `.mod` file, if any, is left as it is, and the session's later steps are kept in memory only.
- **Access:** App, for Persist History in the [Undo History pane](../ui/history_view.hpp.skel.md#class-historyview).

### function: history_state

- **Inputs:** none.
- **Returns:** the [HistoryState](#symbol-historystate); `session_only` whenever persistence is off.
- **State changes:** none.
- **Access:** App and the status line.

### function: version

- **Inputs:** none.
- **Returns:** the change counter.
- **State changes:** none.
- **Access:** App and the highlighters.

### function: path

- **Inputs:** none.
- **Returns:** the real path; empty when untitled. `is_untitled()` says whether there is one.
- **State changes:** none.
- **Access:** App and the status line.

### function: read_only_file

- **Inputs:** none.
- **Returns:** whether the file was opened without write permission.
- **State changes:** none.
- **Access:** the status line.

### function: line_ending

- **Inputs:** none.
- **Returns:** `"\n"` or `"\r\n"`: the line ending found first in the file, LF if none.
- **State changes:** none.
- **Access:** Editor, for Enter and for text pasted from the terminal.

### function: id

- **Inputs:** none.
- **Returns:** a process-unique, never reused document id, used by the [Clipboard](./clipboard.hpp.skel.md#class-clipboard) to recognize content copied from this document.
- **State changes:** none.
- **Access:** Editor and Clipboard.

### function: history_load_ms

- **Inputs:** none.
- **Returns:** the `load_ms` reported by `Sidecar.open` when this document was opened; 0 for an untitled document or when no sidecar was read.
- **State changes:** none.
- **Access:** App, to offer a prune after a slow load.

### function: take_status_message

- **Inputs:** none.
- **Returns:** the latest message from an optional subsystem ("history is open in another mod", "history not copied: <reason>", a sidecar write failure), or empty.
- **State changes:** clears the pending message.
- **Access:** App, once per loop iteration.
