---
role: product
unit: ./file_dialog.hpp.skel.md
stamp: source 1d9f8b52, stand-in a9f55160
---
# module: file_dialog (implementation)

Implements [FileDialog](./file_dialog.hpp.skel.md#class-filedialog). Columns are laid out with [display_width](../text/utf8.hpp.skel.md#function-display_width), cutting a name that is too wide on a cluster boundary and padding with spaces; the emoji icons are two cells wide. Name lookups compare lower-cased names; the File field's completion is drawn in its look with bold taken off and dim put on.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a screen narrower than the fixed text clips each row at its right edge; nothing wraps.
- **Depends on:** [FileDialog](./file_dialog.hpp.skel.md#class-filedialog)
- **Depends on:** [display_width](../text/utf8.hpp.skel.md#function-display_width)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
