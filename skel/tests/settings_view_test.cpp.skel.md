---
role: test
stamp: source 370d7c39, stand-in a2991006
---
# module: settings_view_test

The panel has one row for every setting in the schema, in schema order, each showing its label and value (so a new schema row needs no panel change); rows show current values, not defaults; open, close and Esc; Up, Down, Home, End and the page keys move the selection and stop at the ends; Enter and Space ask for a boolean to be flipped and the view never changes `Settings` itself; Enter on an integer asks for a prompt, and Left and Right step it and stop at the ends of its range; Left and Right do nothing to a boolean and other keys are consumed; the key bindings row shows how many commands were changed and Enter asks for their editor; the `word_wrap` row, added to the schema with no change to the panel, is listed and flips; rendering to a `Screen` draws the rows, highlights the selected one, wraps its help text under the list and draws nothing outside the area; a short area scrolls to keep the selected row visible. A choice row shows its name; Enter, Space and Right step to the next name and Left to the previous, wrapping. Pages stop at the ends, whatever the number of settings.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; the settings are never written (the write seam is a no-op).
- **Depends on:** [SettingsView](../src/ui/settings_view.hpp.skel.md#class-settingsview)
- **Depends on:** [Settings](../src/app/settings.hpp.skel.md#class-settings)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [attr_for](../src/ui/theme.hpp.skel.md#function-attr_for)
- **Depends on:** [Json.parse](../src/syntax/json.hpp.skel.md#function-parse)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
