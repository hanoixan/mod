---
role: product
unit: ./theme.hpp.skel.md
stamp: source de40ee36, stand-in c13829eb
---
# module: theme (implementation)

Implements [the theme](./theme.hpp.skel.md). The default table is a `constexpr` array; the active theme is a function-local static. ANSI color numbers are 0–7, plus 8 for the bright ones. Each color name is one row of the entries table (name, style or modifier, default spec, group); the merge colors, `mergeBanner` to `mergeBothDeletion`, are the last.

- **Owns:** nothing beyond the header's.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** none beyond the header's.
- **Depends on:** [ColorTheme](./theme.hpp.skel.md#class-colortheme)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
