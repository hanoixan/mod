---
role: test
stamp: source cc40c263, stand-in 4d8d625d
---
# module: docs_test

Checks the manual against the code. The manual exists with `index.md` as its root and at least ten pages; every relative link of every page resolves to a file and every anchor to a heading of its target, as read-only mode would follow them (only web addresses are left unfollowed); every page is reachable from `index.md`; `config/settings.json` is exactly the generator's text, which lists every scalar setting with its default and an empty `keymap`; the settings page names every setting of the schema in backticks; the key bindings page has, for every default binding, a line with its key label in backticks and its command's display name. The source tree is found through the compile definition `MOD_SOURCE_DIR`. The colors page names every [color name](../src/ui/theme.hpp.skel.md#function-color_names) in backticks.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none; it only reads files.
- **Depends on:** [scan_markdown](../src/syntax/markdown.hpp.skel.md#function-scan_markdown)
- **Depends on:** [ReadOnlyNav.resolve](../src/app/read_only.hpp.skel.md#function-resolve)
- **Depends on:** [default_settings_json](../src/app/settings.hpp.skel.md#function-default_settings_json)
- **Depends on:** [setting_specs](../src/app/settings.hpp.skel.md#function-setting_specs)
- **Depends on:** [Keymap.default_bindings](../src/app/keymap.hpp.skel.md#function-default_bindings)
- **Depends on:** [command_display_name](../src/app/commands.hpp.skel.md#function-command_display_name)
- **Depends on:** [index.md](../docs/manual/index.md.skel.md)
- **Depends on:** [settings.md](../docs/manual/settings.md.skel.md)
- **Depends on:** [key-bindings.md](../docs/manual/key-bindings.md.skel.md)
- **Depends on:** [config/settings.json](../config/settings.json.skel.md)
- **Depends on:** [color_names](../src/ui/theme.hpp.skel.md#function-color_names)
- **Depends on:** [colors.md](../docs/manual/colors.md.skel.md)
- **Depends on:** [command-line.md](../docs/manual/command-line.md.skel.md)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
