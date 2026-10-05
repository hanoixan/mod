---
role: test
stamp: source f32353f5, stand-in 0a433fd1
---
# module: folder_tree_test

On a real scratch folder: the root, open, then every entry, folders first and by name regardless of case, hidden ones marked; Right opens a folder (read then: a file added later shows when it is opened again), Left closes it, on a file or a closed folder selects the folder holding it, and the root stays open; moving by one, by many, never past either end; a folder that cannot be read says so and stays closed.

- **Owns:** its scratch folders.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [FolderTree](../src/app/folder_tree.hpp.skel.md#class-foldertree)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
