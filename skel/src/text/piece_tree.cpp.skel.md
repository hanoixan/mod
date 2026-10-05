---
role: product
unit: ./piece_tree.hpp.skel.md
stamp: source 3517eeab, stand-in 0e3bbe05
---
# module: piece_tree (implementation)

Implements [PieceTree](./piece_tree.hpp.skel.md#class-piecetree).
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)

#### Implementation guidance

- Nodes live in a `std::vector<Node>` with `uint32_t` child and parent indices and a free list. The balancing scheme is a treap with split and merge, with deterministic priorities from a seeded PRNG (splitmix64) so that tests are reproducible. When `split` cuts a piece in two, the new tail node gets a random priority no higher than the node it was cut from, so it can never rise above that node's ancestors; an equal priority would make every fragment of a piece tie, and deletions alone would then build a chain (and overflow the recursive split).
- Parent indices are kept correct by setting a child's parent whenever its parent's aggregates are recomputed; they serve `record_chunk_lines`, which updates aggregates from a piece up to the root.
- An index from `(buffer, offset)` to the nodes whose piece has an unknown count lets `record_chunk_lines` find the pieces of a chunk without walking the tree.
- Node aggregate: `{bytes, lf_known, unknown_pieces}`. A line query descends using `lf_known` and fails, or resolves when forced, as soon as it would cross a subtree with `unknown_pieces > 0` on its left.
- Seeding: pieces of exactly `chunk_size` bytes (the last one shorter), with hard boundaries. Seeding reads no bytes of the file, not even the byte before a boundary, so opening a file of any size touches nothing beyond the `mmap` itself. A line may therefore straddle two chunks; line queries count line feeds per piece and never assume a chunk holds whole lines. The boundaries match the ones [Document](../edit/document.hpp.skel.md#class-document) records while saving, so a rebased tree has the same chunk layout as a freshly opened one. Rejected: moving each boundary forward to the next LF within 64 KiB, because a file with few line feeds (binary data, a sparse file) then reads 64 KiB per chunk on open, 512 MiB for an 8 GiB file.
- Add-buffer blocks are 64 KiB; a paste that does not fit in the current block gets a new block of `max(64 KiB, paste size)`, and the rest of the old block is left unused.
- Counting line feeds in a span uses `std::count` over bytes, which compiles to SIMD. Use `memchr` for find.
- `read` gathers spans by in-order traversal from the node that contains `offset`.

- **Owns:** node storage and the add-buffer block list.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** aggregate drift after a split or merge. Debug builds run a `validate()` after every mutation of a tree of at most `kValidateMaxNodes` (4096) live nodes, so a huge tree's edits stay O(log n) even there (aggregates, parent links, heap order, no empty piece, and the unknown-count index), and tests run randomized edit sequences against a `std::string` oracle.
- **Depends on:** [PieceTree](./piece_tree.hpp.skel.md#class-piecetree)
- **Unknowns:** none
