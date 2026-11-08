---
role: product
unit: ./view_mode.hpp.skel.md
stamp: source dcee2c4c, stand-in f7df8961
---
# module: read_only (implementation)

Implements [ViewModeNav](./view_mode.hpp.skel.md#class-viewmodenav). Links are searched linearly in the outline, which holds at most the links of 16 MiB of text.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [ViewModeNav](./view_mode.hpp.skel.md#class-viewmodenav)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
