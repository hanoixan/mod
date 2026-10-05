---
role: test
stamp: source 841fd451, stand-in 66369015
---
# module: color_theme_test

Color specs: every color word, `bright-`, `on-` and `on-bright-`, every attribute, words in any order and case, a later color replacing an earlier one, `plain` alone, `bright` only in a modifier's spec, an unknown word and an empty spec refused with the word named. The names: unique, each entry a style or a modifier, every `Style` exactly once. The default theme: `attr_for` gives every default of the tables (the old fixed theme's values unchanged except variable, property, parameter, constant and the new modifiers). Overrides: `apply` takes valid entries and warns once per bad one, `set` refuses an unknown name or bad spec, a spec equal to the default removes the override, `reset` and `reset_all`, `overrides` lists only changes; modifiers merge in order, add attributes, replace the color only when named, and `bright` brightens; the active theme is what `attr_for` reads. In vt100 mode the fallback table: bold, underline and reverse only, no color for any style or modifier, overrides ignored and back when vt100 is off. Darkness: each level's defaults for the reverse-video looks and the page, the selection unchanged, overrides still winning and a spec equal to the level's default no override, `on_page` filling default colors and inverting with the page's reverse, and the vt100 table per level. No default uses dark blue on a dark screen (links, functions, the menu highlight), while paper keeps the plain blues. A session color draws over the saved one, is not among the overrides, and is dropped by set, reset and reset_all. An unfocused split's status line is readable text on a darker band, fixed for each darkness, never dim, and the focused look on a VT100. The bars (status, menu, menuAccel, overflowMarker) and statusUnfocused have the same defaults in every darkness; statusUnfocused is a color of its own, set like any other. In vt100 mode the bars, menus and lists stand out from text (status, menu and listSelected reverse, statusUnfocused bold, menuSelected plain, listSelectedUnfocused underlined) the same in every darkness, the page plain.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** a test that changes the active theme resets it before it ends.
- **Depends on:** [ColorTheme](../src/ui/theme.hpp.skel.md#class-colortheme)
- **Depends on:** [parse_color_spec](../src/ui/theme.hpp.skel.md#function-parse_color_spec)
- **Depends on:** [attr_for](../src/ui/theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
