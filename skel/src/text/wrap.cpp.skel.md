---
role: product
unit: ./wrap.hpp.skel.md
stamp: source 951d35ed, stand-in 7fcfe4f1
---
# module: wrap (implementation)

Implements [wrap](./wrap.hpp.skel.md). `row_start` reads the block that holds the position once and walks its rows from the block's start; `row_end` reads from the row's start to the end of its block.

- **Owns:** nothing.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [WrapLayout](./wrap.hpp.skel.md#class-wraplayout)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
