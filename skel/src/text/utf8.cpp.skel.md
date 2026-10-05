---
role: product
unit: ./utf8.hpp.skel.md
stamp: source 312fdaf7, stand-in c34e166e
---
# module: utf8 (implementation)

Implements [utf8.hpp](./utf8.hpp.skel.md). The width table is `#include`d into an anonymous namespace as a `constexpr` array of `{first, last, width, class, gcb, flags}` ranges, so one binary search gives everything a code point needs. The grapheme rules are a small forward state machine over the previous code point's break property, the parity of the current run of regional indicators, an "Extended_Pictographic Extend* seen" state for GB11, and an "InCB Consonant [Extend Linker]* seen" state for GB9c. Test it against the official `GraphemeBreakTest.txt` for the pinned Unicode version.

- **Owns:** nothing beyond the compiled table.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** the table goes stale relative to the terminal's Unicode version. Regenerate it; see [width_table.inc](./width_table.inc.skel.md).
- **Depends on:** [display_width](./utf8.hpp.skel.md#function-display_width)
- **Depends on:** [next_grapheme_boundary](./utf8.hpp.skel.md#function-next_grapheme_boundary)
- **Depends on:** [prev_grapheme_boundary](./utf8.hpp.skel.md#function-prev_grapheme_boundary)
- **Depends on:** [width table](./width_table.inc.skel.md)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
