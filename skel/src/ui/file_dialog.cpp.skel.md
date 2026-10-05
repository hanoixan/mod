---
role: product
unit: ./file_dialog.hpp.skel.md
stamp: source ae63a7c9, stand-in acde88cb
---
# module: file_dialog (implementation)

Implements [FileDialog](./file_dialog.hpp.skel.md#class-filedialog). Columns are laid out with [display_width](../text/utf8.hpp.skel.md#function-display_width), cutting a name that is too wide on a cluster boundary and padding with spaces; the emoji icons are two cells wide.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a screen narrower than the fixed text clips each row at its right edge; nothing wraps.
- **Depends on:** [FileDialog](./file_dialog.hpp.skel.md#class-filedialog)
- **Depends on:** [display_width](../text/utf8.hpp.skel.md#function-display_width)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
