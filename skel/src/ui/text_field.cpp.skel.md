---
role: product
unit: ./text_field.hpp.skel.md
stamp: source 9141118d, stand-in 6630a1e2
---
# module: text_field (implementation)

Implements [TextField](./text_field.hpp.skel.md#class-textfield). Cursor motion and deletion decode a small window around the cursor, as [Editor](../edit/editor.hpp.skel.md#class-editor) does, and call the grapheme-boundary functions. Drawing uses [display_width](../text/utf8.hpp.skel.md#function-display_width).

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** invalid UTF-8 typed or pasted is kept as is and drawn as `?` by [Screen.print](./screen.hpp.skel.md#function-print).
- **Depends on:** [TextField](./text_field.hpp.skel.md#class-textfield)
- **Depends on:** [display_width](../text/utf8.hpp.skel.md#function-display_width)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
