# The `.mod` sidecar format, version 1

`mod` keeps the complete, branching undo history of an edited file in a *sidecar* file next to it. This document specifies that file completely, so that any program can read or write one without reading `mod`'s source.

## 1. Purpose and naming

The sidecar of a file is named `<file name>.mod`: the file's full name with `.mod` appended (`notes.txt` → `notes.txt.mod`, `a.c` → `a.c.mod`), in the same directory. There is one sidecar per file. It is found by name only: a file renamed by another program starts without history.

A Save As in `mod` copies the sidecar to the new file's name. From then on the two copies evolve independently, so node ids are unique within one sidecar only, and two sidecars can share a common prefix of records.

## 2. Encoding conventions

- Every integer is little-endian and fixed-width: `u8`, `u16`, `u32`, `u64`, and `i64` (two's complement).
- A `hash` is 32 raw bytes: the SHA-256 digest ([FIPS 180-4](https://csrc.nist.gov/pubs/fips/180-4/upd1/final)) of a whole file's content, in standard digest byte order (the order `sha256sum` prints).
- Strings are UTF-8 with a `u32` byte-length prefix. (No record of version 1 contains a string.)
- There is no padding and no alignment anywhere.

## 3. File header (32 bytes)

| Offset | Type | Field | Value |
|---|---|---|---|
| 0 | 8 bytes | magic | `"MODHIST\0"` (`4d 4f 44 48 49 53 54 00`) |
| 8 | `u16` | format_version | 1 |
| 10 | `u16` | hash_algorithm | 1 = SHA-256 |
| 12 | `u32` | flags | 0 (reserved) |
| 16 | `u64` | created_unix_ms | when the file was created, in Unix milliseconds |
| 24 | `u64` | reserved | 0 |

A file whose first 8 bytes are not the magic is not a sidecar; a writer must never modify it. In format version 1, `hash_algorithm` 0 and every value other than 1 are invalid, and a reader must refuse the file. A reader that does not know `format_version` (a newer version) may show what it understands but must not append to the file.

## 4. Record framing

The header is followed by records, back to back, to the end of the file:

| Type | Field |
|---|---|
| `u64` | body_length |
| `u8` | type |
| `body_length` bytes | body |
| `u32` | crc32 |

`crc32` is the IEEE 802.3 CRC-32 (reflected polynomial `0xEDB88320`, initial value and final XOR `0xFFFFFFFF`, as in zlib and PNG) computed over the `type` byte followed by the body.

Records are only ever appended, except that a writer may replace the whole file with a pruned one (section 11). A reader stops at the first record that is truncated (its frame runs past the end of the file) or whose CRC does not match, and treats everything before it as valid. A writer that finds such a tail cuts the file back to the end of the last valid record before it appends.

## 5. Record types

### 1 ROOT (body: 56 bytes)

| Type | Field |
|---|---|
| `u64` | node_id |
| `u64` | base_size |
| `hash` | base_hash |
| `i64` | time_ms |

The start of a history tree. The content at this node is the file whose size is `base_size` and whose SHA-256 is `base_hash`. A sidecar can hold several trees (a *forest*): a new root is started whenever the file on disk cannot be matched to the existing history.

### 2 PAYLOAD (body: raw bytes)

Bytes of an edit, stored out of line. Later NODE records refer to them by the absolute file offset of the body's first byte, and a length.

### 3 NODE

| Type | Field |
|---|---|
| `u64` | id |
| `u64` | parent |
| `i64` | time_ms |
| `u8` | kind |
| `u64` | cursor_before |
| `u64` | cursor_after |
| `u32` | op_count |
| op × `op_count` | ops |

`kind` is the kind of user action, for display only: 0 typing, 1 delete, 2 paste, 3 cut, 4 replace, 5 replace_all, 6 newline, 7 indent, 8 other. A reader shows any other value as "other". `cursor_before` and `cursor_after` are byte offsets in the content before and after the node.

Each op is:

| Type | Field |
|---|---|
| `u64` | offset |
| payload | removed |
| payload | inserted |

and each payload is a `u8 form` followed by:

- form 0, *inline*: `u32 len`, then `len` bytes;
- form 1, *ref*: `u64 file_offset; u64 len`: the bytes `[file_offset, file_offset + len)` of this sidecar, inside an earlier PAYLOAD record's body.

Which form a writer chooses is writer policy, not format: `mod` stores payloads of at most 4096 bytes inline and larger ones in PAYLOAD records, but a reader must accept an inline payload of any `u32` length and a ref of any length.

### 4 SAVE (body: 56 bytes)

| Type | Field |
|---|---|
| `u64` | node_id |
| `u64` | file_size |
| `hash` | file_hash |
| `i64` | time_ms |

The document's content at `node_id` was written to disk; it has this size and hash.

### 5 POSITION

| Type | Field |
|---|---|
| `u64` | current |
| `u32` | n |
| (`u64` parent, `u64` preferred_child) × n | preferences |

The node the user was last at, and redo preferences: after `parent`, redo goes to `preferred_child`. Later records win. `current` is informational: the node a new session starts at is chosen by the loading algorithm (section 7), and redo from there follows the recorded preferences. Without a POSITION record, a parent's preferred child is its most recently created child.

### Unknown types

A reader must skip a record of any other type, using `body_length`. This is the forward-compatibility rule: a later version may add record types without changing `format_version`.

## 6. Semantics

- Applying an op means deleting `removed.len` bytes at `offset` and then inserting the `inserted` bytes at `offset`. Undoing it means deleting `inserted.len` bytes at `offset` and inserting the `removed` bytes there.
- A node's ops are applied in order; undoing a node inverts them in reverse order. Each op's `offset` refers to the content just before that op.
- The content at node *N* is its ROOT's base content with every op on the path from the root to *N* applied.
- Node ids increase strictly through a file, and `parent < id` for every node. A NODE whose parent is not an earlier ROOT or NODE of the same file is invalid and is ignored, as are its descendants.

Because every op carries the bytes it removes and inserts, history stays valid against any saved version of the file: no op refers to bytes of the original file by position.

## 7. Loading algorithm

1. Read the header and check the magic, version and hash algorithm (section 3).
2. Read records in order until the end or the first invalid record (section 4), building the forest: ROOT records create roots, NODE records add children, SAVE records mark save points, POSITION records set redo preferences.
3. Let *S* be the size of the file on disk. The candidate is the **most recent** SAVE record (or ROOT record, by its `base_size`) whose size equals *S*.
4. Compute the SHA-256 of the file on disk and compare it with the candidate's hash. On a match, the document is at the candidate node: undo walks towards the root, and redo follows the preferences.
5. If there is no candidate, or the hash differs, the history is *detached*: the file was changed by something else. A writer appends a new ROOT for the file as it is and continues from there; the older trees remain in the file.

`mod` lets the user edit while step 4 runs in the background, and holds back every record of those edits until the result is known (see section 8).

## 8. Ordering guarantee

- A NODE's parent record always precedes it, and a NODE follows every PAYLOAD record it refers to.
- A ROOT is written only once its `base_hash` is known. Writers buffer the records that follow until then, so readers never see placeholder hashes, and never see a node whose parent may still change.
- A writer creates the sidecar only when it has a first NODE to write, so merely opening a file never creates one. The exception is a copy made for a new file name (Save As in `mod`), which is written whole at once and may hold only a header and a ROOT.

## 9. Concurrency

A writer takes an exclusive advisory lock on the whole file (`flock(LOCK_EX | LOCK_NB)` on POSIX, `LockFileEx` on Windows) for as long as it may append. A second writer that cannot take the lock must not append; it may load the history read-only. Readers need no lock. Writes are appends of whole records; `mod` fsyncs the file once per document save, after the SAVE record, so a power failure can lose the records written since the last save.

## 10. Privacy notice

**The sidecar holds every byte ever deleted from the file.** Text removed from the document, including passwords and keys, stays recoverable by anyone who can read `<file>.mod`. `mod` creates the sidecar with the same permission bits as the edited file and gives it a visible name so its existence is obvious. To purge it, delete the sidecar (Clear History… in `mod`'s Undo History pane), or prune the old part of it (Trim History… in the Undo History pane, section 11). A Save As makes a second copy under the new name.

## 11. Pruning

A writer may replace a sidecar with a smaller one that drops old history. It writes the new file completely under a temporary name in the same directory, then renames it over the old one, so a reader sees either the old file or the new one, never a mixture. The pruned file is an ordinary format-version-1 file (the version does not change), so readers need nothing new. It holds, in this order:

- a fresh header, whose `created_unix_ms` is the old file's;
- one ROOT, whose `node_id` is reused from the node whose content it stands for, so `parent < id` still holds. Its `base_size` and `base_hash` are that node's content, and its `time_ms` is the time of the oldest node kept under it;
- the kept NODE records, in ascending id, each preceded by the PAYLOAD records it refers to. A node whose parent was removed becomes a child of the new root, and its ops become the ops of every removed node between the new root and itself, followed by its own, concatenated in order. Payloads are copied into new PAYLOAD records, because offsets into the old file mean nothing in the new one. Because of this concatenation, a prune does not purge every removed byte: the ops a kept node absorbed still carry them;
- the SAVE records of the kept nodes, in the order they were originally written, so the latest save point is still last. `mod` gives them the time of the prune as `time_ms`;
- one POSITION record.

Node ids are not renumbered and may have gaps. A reader must not assume that a ROOT's `time_ms` is older than every NODE's: it is the age of the oldest kept branch, not when history began. A pruned file has dropped the SAVE records of removed nodes, so a file on disk that matches only one of those starts a new ROOT on the next load (section 7).

## 12. Worked example

The fixture file holds `hello\n` (6 bytes; `sha256sum`: `5891b5b5…6f6be03`). The user typed `big ` at offset 0 (node 2), undid it, typed `!` at offset 5 (node 3, a second branch from the root), and saved `hello!\n` (7 bytes; `sha256sum`: `c8a31cb0…cc13984`). Times are 1 700 000 000 000 ms plus 0, 1000, 2000 and 3000. The tree:

```text
1 ROOT "hello\n"
├── 2 typing: insert "big " at 0      -> "big hello\n"
└── 3 typing: insert "!" at 5         -> "hello!\n"   (saved; preferred)
```

The complete sidecar, 368 bytes, grouped by field (`mod`'s test suite checks that `mod` writes exactly these bytes):

```text
header      4d4f444849535400 0100 0100 00000000 b873e5cf8b010000 0000000000000000
            magic            v1   SHA  flags    created +3000      reserved

ROOT        3800000000000000 01                        body_length 56, type 1
              0100000000000000 0600000000000000        id 1, base_size 6
              5891b5b522d5df086d0ff0b110fbd9d21bb4fc7163af34d08286a2e846f6be03
              0068e5cf8b010000                         time 1700000000000
              c2265073                                 crc32

NODE        4300000000000000 03                        body_length 67, type 3
              0200000000000000 0100000000000000 e86be5cf8b010000 00
                                                       id 2, parent 1, time +1000, typing
              0000000000000000 0400000000000000 01000000
                                                       cursor 0 -> 4, 1 op
              0000000000000000 00 00000000 00 04000000 62696720
                                                       offset 0, removed inline "", inserted inline "big "
              13f67fa0

NODE        4000000000000000 03                        body_length 64, type 3
              0300000000000000 0100000000000000 d06fe5cf8b010000 00
                                                       id 3, parent 1, time +2000, typing
              0500000000000000 0600000000000000 01000000
                                                       cursor 5 -> 6, 1 op
              0500000000000000 00 00000000 00 01000000 21
                                                       offset 5, removed "", inserted "!"
              330406ff

SAVE        3800000000000000 04                        body_length 56, type 4
              0300000000000000 0700000000000000        node 3, file_size 7
              c8a31cb076b21999bd2cdcfa5f446a7a6644de88037087112fa18bd90cc13984
              b873e5cf8b010000                         time +3000
              1447b9cc

POSITION    1c00000000000000 05                        body_length 28, type 5
              0300000000000000 01000000 0100000000000000 0300000000000000
                                                       current 3; after 1, redo goes to 3
              c0d46378
```

Loading it for a 7-byte file: the candidate is the SAVE of node 3, whose hash matches `hello!\n`, so the session starts at node 3. Undo inverts node 3 (deletes the `!`) and reaches the root; redo from the root goes to node 3, its preferred child; the branch to node 2 stays reachable.
