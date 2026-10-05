---
role: product
stamp: source ba165883, stand-in cbdef0f8
---
# module: piece_tree

The text storage engine. A document is a sequence of pieces referring to immutable buffers: mapped files and one append-only add buffer. The pieces live in a balanced binary tree whose nodes are augmented with subtree byte length and subtree line-feed count. All positions are **byte offsets**. Line numbers are derived values and may be *unknown* until the relevant chunks have been scanned. This is what makes files of any size possible.

Design guidance: this module is on the hot path for both rendering and typing. Keep `insert` and `erase` at O(log n) and allocation-light. Never copy original-buffer bytes. Prefer a red-black tree or a treap stored in node arrays with indices, not `shared_ptr` nodes.

- **Owns:** the buffer table (mapped files and the add buffer), the piece tree, and the per-buffer chunk line-count cache.
- **Access:** public. Owned by [Document](../edit/document.hpp.skel.md#class-document). Main thread only, except that buffer *bytes* may be read by worker threads through the `shared_ptr` they captured.
- **Required:** always.
- **Failure modes:** listed per function. Integer overflow is impossible with `uint64_t` offsets. The add buffer can exhaust memory if the user pastes or types more than available RAM. Large *re-inserted* text (from undo) is a piece reference, not a copy, so it does not grow the add buffer.
- **Depends on:** [MappedFile](../platform/file_map.hpp.skel.md#class-mappedfile)
- **Unknowns:** none

## symbol: BufferIndex

`uint32_t`, an index into the tree's buffer table. Index 0 is always the add buffer, and the `original` given at construction, when there is one, is index 1. Entries are never removed during a session, so a `Piece` stays valid as long as the tree lives.

- **Access:** public.

## symbol: Piece

`{ BufferIndex buffer; uint64_t offset; uint64_t length; uint64_t lf_count; }`, with equality. `lf_count == kUnknownLines` (all bits set, a public constant) until it is known.

- **Access:** public. Undo payloads hold `PieceRun`s.

## symbol: PieceRun

`std::vector<Piece>`: a contiguous span of document content expressed as pieces. It is returned by `erase` and accepted by `insert_run`, which makes undo of a huge deletion O(pieces) with no byte copies.

- **Access:** public.
- **Referred by:** [clipboard](../edit/clipboard.hpp.skel.md)
- **Referred by:** [undo_tree](../edit/undo_tree.hpp.skel.md)

## symbol: Chunk

`{ uint64_t offset; uint64_t length; }`: one slice of a buffer as the tree was seeded with it. The chunks of a buffer are in order and tile it.

- **Access:** public.

## symbol: ChunkLines

`{ uint64_t offset; uint64_t length; uint64_t lf_count; }`: a chunk of a saved file with the line-feed count recorded while saving. Input to `rebase`.

- **Access:** public.

## symbol: FrozenBytes

`{ std::span<const std::byte> bytes; std::shared_ptr<const void> keep_alive; }`: the result of `frozen_bytes`. `keep_alive` owns the storage behind `bytes` (the mapping, or the add-buffer block).

- **Access:** public.

## class: PieceTree

- **Inputs:** `original`: a `shared_ptr<const MappedFile>`, possibly null or an empty file; `chunk_size`: default 1 MiB (`kDefaultChunkSize`). A non-null `original` becomes buffer 1 and is seeded as described in [piece_tree.cpp](./piece_tree.cpp.skel.md). `npos` (all bits set) is the public "not found" offset.
- **State changes:** invariant: the in-order concatenation of the pieces is the document. Every node's aggregates (bytes, known line feeds, count of unknown pieces) equal the sum over its subtree. No piece has length 0. Add-buffer storage is a list of fixed-size blocks (64 KiB, or larger for big pastes) that are never reallocated, and no piece crosses a block boundary, so pointers handed to readers stay valid.
- **Owns:** the nodes, the buffer table, and the add-buffer blocks.
- **Access:** main thread. `frozen_bytes` is the only cross-thread view.
- **Referred by:** [document](../edit/document.hpp.skel.md)
- **Referred by:** [piece_tree (implementation)](./piece_tree.cpp.skel.md)
- **Referred by:** [piece_tree_test](../../tests/piece_tree_test.cpp.skel.md)
- **Referred by:** [wrap](./wrap.hpp.skel.md)
- **Referred by:** [wrap_test](../../tests/wrap_test.cpp.skel.md)
- **Referred by:** [markdown_links_test](../../tests/markdown_links_test.cpp.skel.md)
- **Referred by:** [read_only_test](../../tests/read_only_test.cpp.skel.md)

### function: insert

- **Inputs:** `offset`: 0 ≤ offset ≤ `size()`; `bytes`: non-empty.
- **Returns:** nothing.
- **State changes:** appends `bytes` to the add buffer (computing its line-feed count), splits the piece at `offset` if needed, inserts the new pieces, and rebalances. Consecutive typing that appends to the end of the last add-buffer piece extends that piece in place.
- **Access:** [Document.apply](../edit/document.hpp.skel.md#function-apply) only. All edits go through the document so that undo records them.

### function: insert_run

- **Inputs:** `offset`; `run`: pieces that refer to buffers already in this tree's table.
- **Returns:** nothing.
- **State changes:** inserts the pieces as they are, with no byte copy.
- **Access:** [Document](../edit/document.hpp.skel.md#function-apply), when undoing a deletion or redoing an insertion.

### function: erase

- **Inputs:** `offset`, `length`, with `offset + length ≤ size()`.
- **Returns:** the removed content as a `PieceRun`.
- **State changes:** splits the boundary pieces, removes the covered pieces, and rebalances.
- **Access:** as `insert`.

### function: pieces

- **Inputs:** `offset`, `length`, with `offset + length ≤ size()`.
- **Returns:** the `PieceRun` that covers `[offset, offset + length)`, without changing the tree. The end pieces are trimmed; a trimmed piece whose count was known gets the count of its part (bounded by the piece's length), otherwise `kUnknownLines`.
- **State changes:** none.
- **Access:** [Editor.copy](../edit/editor.hpp.skel.md#function-copy) (a selection as a run, with no byte copy) and [Document.apply](../edit/document.hpp.skel.md#function-apply) (the run of a large inserted text).

### function: store

- **Inputs:** `bytes`: any length, possibly empty; they may point into this tree's own buffers.
- **Returns:** a `PieceRun` in the add buffer holding a copy of `bytes`, with known line-feed counts. The run is **not** inserted into the document.
- **State changes:** appends to the add buffer (new blocks as needed; existing blocks never move).
- **Access:** [Document](../edit/document.hpp.skel.md#function-save), to materialize runs that point into a mapping an in-place save is about to overwrite, and to insert clipboard content copied from another document.

### function: add_buffer

- **Inputs:** `file`: a `shared_ptr<const MappedFile>`, such as a remapped sidecar payload area.
- **Returns:** the new `BufferIndex`.
- **State changes:** appends to the buffer table.
- **Access:** [Sidecar](../edit/sidecar.hpp.skel.md#class-sidecar) payload rebinding, through Document.

### function: release_buffer

- **Inputs:** `buffer`: a file buffer's index that no piece uses any more (the caller guarantees it).
- **Returns:** nothing.
- **State changes:** drops the buffer's mapping and its line-count caches, so the mapping can close; the index is never reused. Index 0 (the add buffer) and unknown indexes are ignored.
- **Access:** [Document.save](../edit/document.hpp.skel.md#function-save), before an in-place write.

### function: size

- **Inputs:** none.
- **Returns:** the document length in bytes, in O(1).
- **State changes:** none.
- **Access:** public.

### function: read

Streams document bytes.

- **Inputs:** `offset`, `length`; `sink`: a callback `bool(std::span<const std::byte>)` that returns false to stop.
- **Returns:** nothing.
- **State changes:** none.
- **Access:** main thread: rendering, search, save, and LSP text. Spans point straight into the buffers, with no copy. A convenience overload copies into a `std::string` and must only be used for bounded lengths (one screen line, a selection under a cap).
- **Failure modes:** reading mapped pages of a file that another process truncated can fault; see [file_map](../platform/file_map.hpp.skel.md).
- **Referred by:** [search](../search/search.hpp.skel.md)
- **Referred by:** [markdown](../syntax/markdown.hpp.skel.md)
- **Referred by:** [LspClient.did_open](../syntax/lsp_client.hpp.skel.md#function-did_open)
- **Referred by:** [semantic_highlighter](../syntax/semantic_highlighter.hpp.skel.md)
- **Referred by:** [syntax_highlighter](../syntax/syntax_highlighter.hpp.skel.md)

### function: byte_at

- **Inputs:** `offset` < `size()`.
- **Returns:** `std::byte`, in O(log n).
- **State changes:** none.
- **Access:** public.

### function: find_lf_forward

- **Inputs:** `from`: an offset; `limit`: the maximum number of bytes to scan.
- **Returns:** the offset of the next `\n` in `[from, from + limit)`, or `npos` if none is found within `limit` or before end of file. The scan uses `memchr` over the piece spans.
- **State changes:** none.
- **Access:** [Editor](../edit/editor.hpp.skel.md#function-move) for line motions and [EditorView](../ui/editor_view.hpp.skel.md#function-render) for row layout. Neither needs global line numbers.
- **Referred by:** [EditorView.render](../ui/editor_view.hpp.skel.md#function-render)

### function: find_lf_backward

- **Inputs:** `from`, `limit`.
- **Returns:** the offset of the previous `\n` in `[from - limit, from)` (clamped at 0), or `npos`.
- **State changes:** none.
- **Access:** as `find_lf_forward`.
- **Referred by:** [Editor.move](../edit/editor.hpp.skel.md#function-move)

### function: line_of

- **Inputs:** `offset`; `force`: bool.
- **Returns:** the 0-based line number, as `std::optional<uint64_t>`. It returns `nullopt` if any piece before `offset` has an unknown count and `force` is false. With `force`, unknown pieces on the path are counted synchronously and cached.
- **State changes:** with `force`, it caches resolved piece counts and node aggregates.
- **Access:** [EditorView](../ui/editor_view.hpp.skel.md#function-render) uses `force=false` for the gutter and status line. Go-to-line uses `force=true`. The LSP highlighter keeps its own line index instead; see [SemanticHighlighter](../syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter).
- **Failure modes:** `force` on a multi-GB unscanned prefix blocks the UI for seconds. Callers only force for explicit user commands.
- **Referred by:** [editor_view (implementation)](../ui/editor_view.cpp.skel.md)

### function: line_start

- **Inputs:** `line`: 0-based; `force`: bool.
- **Returns:** `std::optional<uint64_t>`: the offset of that line's first byte. It returns `nullopt` if the line is beyond the end of file, or if it is unknown and not forced.
- **State changes:** as `line_of`.
- **Access:** Go-to-line in the [Prompt](../ui/prompt.hpp.skel.md#class-prompt) flow.

### function: line_count

- **Inputs:** none.
- **Returns:** `std::optional<uint64_t>`: the number of LFs plus 1, or `nullopt` while any count is unknown.
- **State changes:** none.
- **Access:** gutter width and status line.

### function: record_chunk_lines

- **Inputs:** `buffer`, `chunk_offset`, `chunk_length`, `lf_count`.
- **Returns:** nothing.
- **State changes:** stores the count in the chunk cache. Every piece that still exactly covers that chunk gets `lf_count` and updates its aggregates. Pieces that were split from the chunk before its count arrived are counted now (the work is bounded by the chunk's length), and a piece of an already-recorded chunk that is later inserted again (`insert_run`) gets its count at insertion, so once every chunk of a buffer is recorded no piece of it stays unknown.
- **Access:** main thread, from closures posted by [LineScanner](./line_scanner.hpp.skel.md#class-linescanner).

### function: frozen_bytes

- **Inputs:** `buffer`, `offset`, `length`. The range lies within one piece's storage; for the add buffer that means one block, which every piece satisfies.
- **Returns:** a `FrozenBytes`: a `std::span` plus a keep-alive `shared_ptr` that a worker thread can read safely. Mapped buffers and already-written add-buffer blocks are immutable.
- **State changes:** none.
- **Access:** called on the main thread. The returned view is used on the [Sidecar](../edit/sidecar.hpp.skel.md#class-sidecar) writer thread.

### function: chunks

- **Inputs:** `buffer`.
- **Returns:** `std::vector<Chunk>`: the chunks `buffer` was seeded with, in order; empty for the add buffer, for a buffer added with `add_buffer`, and for an empty file.
- **State changes:** none.
- **Access:** [Document](../edit/document.hpp.skel.md#class-document), to give [LineScanner](./line_scanner.hpp.skel.md#class-linescanner) the chunk list.
- **Referred by:** [LineScanner](./line_scanner.hpp.skel.md)

### function: rebase

Replaces the whole tree after a save, so that later edits refer to the new file.

- **Inputs:** `file`: the newly saved mapping; `chunk_lines`: a `std::span<const ChunkLines>`, the per-chunk LF counts recorded while saving. The chunks must tile `file` in order; their boundaries are the saver's choice and become `chunks()` of the new buffer. If they do not tile it (a caller bug, asserted in debug builds), the file is seeded with unknown counts instead. An **empty** `chunk_lines` is not a bug: it asks for exactly that, the file chunked and seeded with unknown counts as the constructor does, so that [Document.reload](../edit/document.hpp.skel.md#function-reload) can replace the content and start a [LineScanner](./line_scanner.hpp.skel.md#class-linescanner) on `chunks()` of the new buffer.
- **Returns:** the `BufferIndex` given to `file`.
- **State changes:** discards all nodes and seeds one piece per chunk of `file` with known counts. The buffer table keeps old entries, because undo payloads may still reference them, and adds `file`.
- **Access:** [Document.save](../edit/document.hpp.skel.md#function-save), and [Document.reload](../edit/document.hpp.skel.md#function-reload) with an empty `chunk_lines`.
