---
role: product
unit: ./keymap_view.hpp.skel.md
stamp: source a77699e5, stand-in 2fc9f20f
---
# module: keymap_view (implementation)

Implements [KeymapView](./keymap_view.hpp.skel.md#class-keymapview). The filter lower-cases the search, splits it at spaces, and keeps the bindable commands whose display name, internal name and key labels together contain every word. The key column starts three columns after the longest display name.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [KeymapView](./keymap_view.hpp.skel.md#class-keymapview)
- **Depends on:** [decode_before](../text/utf8.hpp.skel.md#function-decode_before)
- **Depends on:** [to_utf8](./input.hpp.skel.md#function-to_utf8)
- **Depends on:** [list_step](./list_cursor.hpp.skel.md#function-list_step)
- **Depends on:** [scroll_to_show](./list_cursor.hpp.skel.md#function-scroll_to_show)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
