---
role: test
stamp: source 841b4b20, stand-in 48336519
---
# module: history_model_test

Model-based tests of the history: seeded random editing sessions on a real Document (typing that coalesces, deletions, replacements, undo, redo, branch cycling, jumps, saves) where every node must give back exactly the text it was left with, before and after the session is written to its sidecar and reopened; and the resulting sidecar cut short at every byte, or with random bytes flipped, must open without harm, the file untouched and any recovered history agreeing with the model.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [Sidecar](../src/edit/sidecar.hpp.skel.md#class-sidecar)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
