---
role: product
---
# data: settings

A reference `settings.json` with every setting at its default, installed as `<prefix>/share/mod/settings.json` for users to copy into their configuration folder and edit. mod never reads this copy; its defaults live in the code's [schema](../src/app/settings.hpp.skel.md#function-setting_specs). The checked-in copy is for reading in the repository and must equal what the generator prints.

- **Source:** generated — by [gen_default_settings](../tools/gen_default_settings.cpp.skel.md) from the schema, through [default_settings_json](../src/app/settings.hpp.skel.md#function-default_settings_json).
- **Required:** optional — a reference only.
- **Failure modes:** the checked-in copy drifts after a schema change; [docs_test](../tests/docs_test.cpp.skel.md) compares it with the generator's output.
- **Depends on:** [default_settings_json](../src/app/settings.hpp.skel.md#function-default_settings_json)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../CMakeLists.txt.skel.md)
- **Referred by:** [docs_test](../tests/docs_test.cpp.skel.md)

## Schema

```json
{
  "tab_width": 4,
  "line_numbers": true,
  "syntax_coloring": true,
  "word_wrap": true,
  "terminal_mode": "auto",
  "darkness": "normal",
  "cursor_style": "bar",
  "tab_inserts": "spaces",
  "keymap": {}
}
```

One member per scalar setting of the schema, in schema order, booleans as JSON booleans, then an empty `keymap`; two-space indent, one member per line, a final newline.

## Generation

The build runs [gen_default_settings](../tools/gen_default_settings.cpp.skel.md), which writes `${CMAKE_BINARY_DIR}/generated/settings.json`; `cmake --install` installs that file. After changing the schema, refresh the checked-in copy:

```bash
cmake --build --preset linux-debug --target default_settings
cp build/linux-debug/generated/settings.json config/settings.json
```
