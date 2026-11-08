---
role: test
stamp: source 90b4b152, stand-in b9f20105
---
# module: piece_tree_test

Randomized differential testing against a `std::string` oracle: 100k seeded random inserts, erases and runs. It also covers: the `line_of`/`line_start` round trip; unknown-count pieces with `force` and without it; `record_chunk_lines` after splits; `rebase`; zero-length files; CRLF; and a sparse 8 GiB file (created with `ftruncate`) opened and edited at both ends without RSS growth. 100 000 one-byte erases inside one large piece keep the tree balanced: no crash, no linear time per edit. A file read in (FileLoad::read_in) keeps its bytes after it is rewritten in place and deleted, its identity matching stat_path; empty and folder cases.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [PieceTree](../src/text/piece_tree.hpp.skel.md#class-piecetree)
- **Depends on:** [MappedFile](../src/platform/file_map.hpp.skel.md#class-mappedfile)
- **Depends on:** [LineScanner](../src/text/line_scanner.hpp.skel.md#class-linescanner)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
