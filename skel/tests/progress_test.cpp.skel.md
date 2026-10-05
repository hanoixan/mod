---
role: test
stamp: source 09bd6403, stand-in 071a7923
---
# module: progress_test

[format_progress](../src/util/progress.hpp.skel.md#function-format_progress): with and without a total, each unit boundary (1023 B, 1.0 KiB, 1.0 MiB, 1.0 GiB, 1.0 TiB), a `done` past `total` clamped to 100%, and a zero total. The plumbing, each through a recording sink: [Sidecar.copy_to](../src/edit/sidecar.hpp.skel.md#function-copy_to) of a sidecar with a multi-MiB payload reports `"copying history"` with non-decreasing `done` and a final `done` equal to the bytes copied; [Sidecar.rewrite](../src/edit/sidecar.hpp.skel.md#function-rewrite) (through [Document.prune_history](../src/edit/document.hpp.skel.md#function-prune_history)) reports `"pruning history"`, and, when the anchor needs a silent walk, `"hashing"` with a total equal to the size of the anchor's content and a last `done` equal to it; [Sidecar.flush](../src/edit/sidecar.hpp.skel.md#function-flush) with an empty queue reports nothing; and an empty sink is never called.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Clocks are injected, and `EventQueue.drain` runs worker results deterministically; no check depends on how many reports a slow or fast disk produces, only on their order and their last value.
- **Depends on:** [format_progress](../src/util/progress.hpp.skel.md#function-format_progress)
- **Depends on:** [Sidecar.copy_to](../src/edit/sidecar.hpp.skel.md#function-copy_to)
- **Depends on:** [Sidecar.rewrite](../src/edit/sidecar.hpp.skel.md#function-rewrite)
- **Depends on:** [Sidecar.flush](../src/edit/sidecar.hpp.skel.md#function-flush)
- **Depends on:** [Document.prune_history](../src/edit/document.hpp.skel.md#function-prune_history)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
