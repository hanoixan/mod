---
role: test
stamp: source e26a1ae2, stand-in 18c919d1
---
# module: piece_tree_test

Randomized differential testing against a `std::string` oracle: 100k seeded random inserts, erases and runs. It also covers: the `line_of`/`line_start` round trip; unknown-count pieces with `force` and without it; `record_chunk_lines` after splits; `rebase`; zero-length files; CRLF; and a sparse 8 GiB file (created with `ftruncate`) opened and edited at both ends without RSS growth. 100 000 one-byte erases inside one large piece keep the tree balanced: no crash, no linear time per edit.

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
