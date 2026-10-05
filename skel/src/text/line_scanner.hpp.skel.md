---
role: product
stamp: source 2e7162b4, stand-in 853f0282
---
# module: line_scanner

A background worker that walks a mapped file once. It counts line feeds per chunk, so that line numbers become known, and computes the whole-file content hash, so that the sidecar can match history to disk. Both come from a single sequential pass over the mapping.

- **Owns:** the scanner thread and its cancellation flag.
- **Access:** public. [Document](../edit/document.hpp.skel.md#class-document) creates one per opened mapping and cancels it on close or reopen. On save it is left running if the base hash is still pending; see [Document.save](../edit/document.hpp.skel.md#function-save).
- **Required:** always.
- **Failure modes:** cancellation latency: the flag is checked at least once per chunk. A file truncated while it is being scanned faults; see [file_map](../platform/file_map.hpp.skel.md). The scan of a cold multi-GB file on a slow disk competes with the UI's page faults, which is accepted. It reads sequentially with `MADV_SEQUENTIAL`.
- **Depends on:** [EventQueue.post](../util/event_queue.hpp.skel.md#function-post)
- **Depends on:** [ContentHasher](../util/hash.hpp.skel.md#class-contenthasher)
- **Depends on:** [MappedFile](../platform/file_map.hpp.skel.md#class-mappedfile)
- **Depends on:** [PieceTree.chunks](./piece_tree.hpp.skel.md#function-chunks)
- **Unknowns:** none

## class: LineScanner

- **Inputs:** `file`: the mapping; `buffer`: its `BufferIndex` in the tree; `chunks`: the `std::vector<Chunk>` the tree was seeded with ([PieceTree.chunks](./piece_tree.hpp.skel.md#function-chunks)); `queue`: an `EventQueue&`; `on_chunk`: a `std::function<void(BufferIndex, uint64_t chunk_offset, uint64_t chunk_length, uint64_t lf_count)>` run on the main thread, with the same arguments as [PieceTree.record_chunk_lines](./piece_tree.hpp.skel.md#function-record_chunk_lines) so that it can forward directly; `on_done`: a `std::function<void(Result<ContentHash>)>` run on the main thread with the whole-file hash, or with an `internal` error when the scan failed. It is not called when the scan is canceled.
- **State changes:** `idle → running → (done | canceled)`. Results are posted in batches of about 16 chunks, or every 50 ms, to limit wakeups. The callbacks are held in a shared object that each posted closure captures, so a closure that runs after the scanner is destroyed never refers back to it; the callbacks themselves must stay valid until the queue is drained or closed.
- **Owns:** the `std::jthread`.
- **Access:** constructed and destroyed on the main thread. The destructor requests a stop and joins.
- **Referred by:** [document (implementation)](../edit/document.cpp.skel.md)
- **Referred by:** [Document.open](../edit/document.hpp.skel.md#function-open)
- **Referred by:** [line_scanner (implementation)](./line_scanner.cpp.skel.md)
- **Referred by:** [piece_tree_test](../../tests/piece_tree_test.cpp.skel.md)

### function: start

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** launches the thread.
- **Access:** once, after construction.

### function: cancel

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** requests a stop. Closures that were already posted still run, so receivers must check that the tree's `buffer` is still current. They do this by comparing a generation number passed in the closure.
- **Access:** main thread.
