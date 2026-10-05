---
role: test
stamp: source 29c833ab, stand-in d00f875b
---
# module: document_list_test

Documents keep their opening order and the newest is shown; showing never reorders; closing the shown document shows the one viewed most recently before it, down to none; removing a document that is not shown keeps the shown one; an unknown id changes nothing; ids are never reused.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [DocumentList](../src/app/document_list.hpp.skel.md#class-documentlist)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
