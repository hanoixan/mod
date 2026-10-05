---
role: product
unit: ./doc_search.hpp.skel.md
stamp: source 2a3008da, stand-in e6b6b21f
---
# module: doc_search (implementation)

Implements [doc_search](./doc_search.hpp.skel.md). The two compiled-in folders come from `MOD_INSTALL_DOC_DIR` and `MOD_SOURCE_DOC_DIR` ([CMakeLists.txt](../../CMakeLists.txt.skel.md)); without them they are empty and left out.

- **Owns:** nothing.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [doc_search](./doc_search.hpp.skel.md)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
