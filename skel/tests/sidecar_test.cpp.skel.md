---
role: test
stamp: source 82638f55, stand-in a9c471dd
---
# module: sidecar_test

Round trip: write history, reopen, and check the tree and current node are identical. Truncated-tail recovery; a CRC mismatch; a foreign file with bad magic is never modified; the lock held by a second instance gives read-only; a newer version gives read-only; unknown record types are skipped. A golden-file test against the worked example in the format doc. Large payloads written by the writer thread and rebound to `SidecarRef`. `copy_to`: the new sidecar is byte-identical to the old plus any unpersisted session nodes, `SidecarRef`s resolve in the copy, the old sidecar is left unchanged, records still deferred at copy time land only in the new file, a foreign `<new>.mod` is never touched, a valid unlocked `<new>.mod` belonging to another file's history is replaced by the copy, and a valid `<new>.mod` locked by a second instance is left unchanged with the new `Sidecar` read-only. `copy_to` from a `pathless` sidecar (an untitled document's first Save As) writes a fresh header, the ROOT and every session node. `clear` deletes the file, drops deferred and queued records, writes a new file only on the next append, never touches a foreign file, and refuses in `read_only`. Inline payloads are used up to exactly 4096 bytes and PAYLOAD records from 4097. `fsync` is called once after a SAVE record and not after NODE records (through an injected file-operations seam); a short write disables persistence and reports it. Golden ROOT and SAVE records with SHA-256 hashes, cross-checked against `sha256sum` of the fixture file. `open` reports `load_ms` from the injected `now_ms` (a clock that advances 6 000 ms during the read gives 6000; a clock stepping backwards gives 0).

`rewrite`:
- **Output.** The file is smaller. Reopening it yields exactly the pruned tree: one ROOT with the anchor's id, base and the oldest top's time; the absorbed ops; the kept SAVE records with the latest last; and the POSITION. Its `candidate` is the latest save point.
- **Payloads.** `SidecarRef` payloads are copied into new PAYLOAD records, and the returned `Rebind`s resolve in the new file. Inline and `Pieces` payloads keep their form rules. Pieces read from the old mapping before the rewrite still read correctly afterwards.
- **Permissions and lock.** The new file has the document's permission bits and holds the lock, and a second instance opening it gets `read_only`.
- **Refusals and failures.** A hard-linked sidecar gives `not_atomic` with the old file byte-identical. A write that fails through the seams while `rewrite` drains the writer queue disables the sidecar, and `rewrite` then refuses without renaming anything, so the old file is intact (same inode, same bytes). The temp-file write inside `write_atomically` has no seam; the hard-link case covers its failure path. `rewrite` is refused in `read_only`.
- **Appends.** Appends after a rewrite go to the new file.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [Sidecar](../src/edit/sidecar.hpp.skel.md#class-sidecar)
- **Depends on:** [Sidecar.rewrite](../src/edit/sidecar.hpp.skel.md#function-rewrite)
- **Depends on:** [sidecar format](../docs/sidecar-format.md.skel.md)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
