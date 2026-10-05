---
role: test
stamp: source a7a2e219, stand-in 17a8a3b2
---
# module: process_test

A spawned child starts with SIGPIPE at its default though the parent ignores it (a shell that signals itself dies instead of printing); a child's output arrives and its working directory is the one asked for.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [ChildProcess](../src/platform/process.hpp.skel.md#class-childprocess)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
