---
role: test
stamp: source 29261be4, stand-in 4c9ad9b0
---
# module: workspace_test

One empty view to start; opening parks and showing takes back; a file shown only in a view that is not focused is still found (never opened twice); a view following a link still counts as showing its document (releasing another view of it never parks it too, and parking keeps the followed link); removing the focused view moves the focus up, else down; trimming parks only documents no remaining view shows; closing removes a document's other views; `unsaved` lists a dirty document once however many views show it. `find` knows a document by any name of its file: relative, through a symlink, a hard link, or a dangling symlink it was opened through.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [Workspace](../src/app/workspace.hpp.skel.md#class-workspace)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
