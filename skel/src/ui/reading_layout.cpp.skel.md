---
role: product
unit: ./reading_layout.hpp.skel.md
stamp: source b7a449b4, stand-in 477446cb
---
# module: reading_layout (implementation)

Implements [reading_layout](./reading_layout.hpp.skel.md). The positions are kept sorted by source offset for `locate` and Left/Right, and per line by column for the vertical motions.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional.
- **Failure modes:** none.
- **Depends on:** [ReadingLayout](./reading_layout.hpp.skel.md#class-readinglayout)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
