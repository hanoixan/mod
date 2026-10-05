---
role: product
stamp: source 63208a53, stand-in 18a67560
---
# resource: sidecar-format.md

The **public specification** of the `.history` sidecar format. It exists so that "anyone can load it": other users, other tools, and future versions of `mod`. It must be complete enough to write an independent reader and writer from it alone, without reading `mod`'s source.

The document must contain the following:

1. **Purpose and naming:** `<file name>.history`, in the same directory as the edited file. One sidecar per file. A Save As copies the sidecar to the new file's name; the copies then evolve independently, so node ids are unique within one sidecar only, and two sidecars can share a common prefix of records.
2. **Encoding conventions:** every integer is little-endian and fixed-width. Strings are UTF-8 with a `u32` length prefix. There is no padding.
3. **File header (32 bytes):** magic `"MODHIST\0"` (8 bytes), `u16 format_version` (= 1), `u16 hash_algorithm` (1 = SHA-256; 0 and every other value are invalid in format version 1, and a reader must refuse the file), `u32 flags` (reserved, 0), `u64 created_unix_ms`, `u64 reserved`. A reader that does not know `format_version` must refuse to append.
   The `hash` type used in the records below is 32 raw bytes: the SHA-256 digest ([FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)) of the whole file content, in standard digest byte order (the order `sha256sum` prints).
4. **Record framing:** `u64 body_length`, `u8 type`, then the body, then `u32 crc32` over `type || body`. The CRC is [crc32](../src/util/hash.hpp.skel.md#function-crc32) (IEEE). Records only ever get appended, except that a writer may replace the whole file with a pruned one (item 11). A reader stops at the first record that is truncated or has a bad CRC, and treats everything before it as valid.
5. **Record types:**
   - `1 ROOT { u64 node_id; u64 base_size; hash base_hash; i64 time_ms }` (body: 56 bytes): the start of a history tree. The document content is the file whose size and hash match.
   - `2 PAYLOAD { raw bytes }`: referenced by later NODE records using the absolute file offset of the body's first byte.
   - `3 NODE { u64 id; u64 parent; i64 time_ms; u8 kind; u64 cursor_before; u64 cursor_after; u32 op_count; op[op_count] }`. `kind` is the kind of user action, for display only: 0 typing, 1 delete, 2 paste, 3 cut, 4 replace, 5 replace_all, 6 newline, 7 indent, 8 other; a reader shows any other value as "other". Each op is `{ u64 offset; payload removed; payload inserted }`, and each payload is `u8 form` (0 = inline: `u32 len` followed by the bytes; 1 = ref: `u64 file_offset; u64 len`). Which form a writer chooses is writer policy, not format: `mod` inlines payloads of at most 4096 bytes, but a reader must accept an inline payload of any `u32` length and a ref of any length.
   - `4 SAVE { u64 node_id; u64 file_size; hash file_hash; i64 time_ms }` (body: 56 bytes): the document's content at `node_id` was written to disk.
   - `5 POSITION { u64 current; u32 n; (u64 parent, u64 preferred_child)[n] }`: the last known cursor in the tree, and redo preferences. Later records win. `current` is informational: the node a new session starts at is chosen by the loading algorithm (section 7), and redo from there follows the recorded preferences.
   - Unknown record types must be skipped using `body_length`. This is the forward-compatibility rule.
6. **Semantics:** applying an op means deleting `removed.len` bytes at `offset` and then inserting `inserted`. A node's ops are applied in order, and undoing a node inverts them in reverse order. The content at node *N* is its ROOT's base content with every op on the path from the root to *N* applied. Node ids increase strictly, and `parent < id`.
7. **Loading algorithm:** the steps to find which node matches the file on disk. Match the size against SAVE records (and ROOT records), then confirm the hash. If nothing matches, the history is detached: a writer appends a new ROOT.
8. **Ordering guarantee:** a NODE's parent record always precedes it, and a ROOT is written only once its `base_hash` is known. Writers buffer earlier records until then, so readers never see placeholder hashes.
9. **Concurrency:** writers take an exclusive advisory lock on the whole file (`flock` / `LockFileEx`). Readers need no lock.
10. **Privacy notice:** the sidecar holds every deleted byte.
11. **Pruning:** a writer may replace a sidecar with a smaller one that drops old history. It writes the new file completely under a temporary name in the same directory, then renames it over the old one, so a reader sees either the old file or the new one. The pruned file is an ordinary format-version-1 file (the version does not change), so readers need nothing new. Its contents:
    - one ROOT, whose `node_id` is reused from the node whose content it stands for, so `parent < id` still holds;
    - NODE records whose first steps may carry the ops of several removed nodes, concatenated in order;
    - the SAVE records of the kept nodes;
    - one POSITION record.

    Node ids are not renumbered and may have gaps. A reader must not assume that a ROOT's `time_ms` is older than every NODE's: it is the age of the oldest kept branch, not when history began. A pruned file has dropped the SAVE records of removed nodes, so a file on disk that matches only one of those starts a new ROOT on the next load.
12. **A worked example:** a hex dump of a sidecar holding a 3-node history with one branch, annotated record by record. It is produced by `mod` itself with an injected clock, so [sidecar_test](../tests/sidecar_test.cpp.skel.md) can compare `mod`'s output with it byte for byte, and its ROOT and SAVE hashes are the SHA-256 of the fixture content shown beside it.

- **Required:** always.
- **Referred by:** [Sidecar.rewrite](../src/edit/sidecar.hpp.skel.md#function-rewrite)
- **Failure modes:** the spec drifts from the implementation. [sidecar_test](../tests/sidecar_test.cpp.skel.md) includes a golden-file test whose bytes are the worked example from this document.
- **Depends on:** [ContentHash](../src/util/hash.hpp.skel.md#symbol-contenthash)
- **Referred by:** [sidecar (implementation)](../src/edit/sidecar.cpp.skel.md)
- **Referred by:** [sidecar](../src/edit/sidecar.hpp.skel.md)
- **Referred by:** [sidecar_test](../tests/sidecar_test.cpp.skel.md)
- **Unknowns:** none
