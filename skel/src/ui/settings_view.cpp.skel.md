---
role: product
unit: ./settings_view.hpp.skel.md
stamp: source 2eac5e61, stand-in 4373eef5
---
# module: settings_view (implementation)

Implements [SettingsView](./settings_view.hpp.skel.md#class-settingsview). The value column starts three columns after the longest label of the schema. Help text is wrapped at spaces to the area width less a margin; it is drawn in the gutter style, one blank row under the list.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [SettingsView](./settings_view.hpp.skel.md#class-settingsview)
- **Depends on:** [list_step](./list_cursor.hpp.skel.md#function-list_step)
- **Depends on:** [scroll_to_show](./list_cursor.hpp.skel.md#function-scroll_to_show)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
