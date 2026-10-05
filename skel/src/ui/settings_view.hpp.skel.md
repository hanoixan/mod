---
role: product
stamp: source 0c15c44e, stand-in 5bed4d67
---
# module: settings_view

The User Settings panel, opened with Options > User Settings…. It replaces the text area with a framed, full-width list: one row for **every row of the settings schema** ([setting_specs](../app/settings.hpp.skel.md#function-setting_specs)), in schema order, and nothing else decides what is listed, so a setting added to the schema appears here with no change to this module. Each row shows the setting's label and its current value: `[x]` or `[ ]` for a boolean, the number for an integer, and for the structured `keymap` row how many commands have keys of their own ("defaults" or "3 changed"). The selected row is highlighted and marked with `>`, and its help text is word-wrapped under the list.

```text
 File  Edit  View  Options  Help
┌── User Settings ────────────────────────┐
│  Tab width                    4         │
│> Line numbers on open         [x]       │
│  Markdown formatting…         [x]       │
│                                         │
│  Show the line-number gutter in each    │
│  document as it is opened. View > Line  │
│  Numbers changes it for that document.  │
└─────────────────────────────────────────┘
 Up/Down: move  Enter: change  Left/Right: step a number  Esc: close
```

Keys: Up, Down, Home, End, PageUp and PageDown move the selection and stop at the ends. Enter or Space flips a boolean. Enter, Space and Right step a choice to its next name (wrapping), Left to the previous; the row shows the name. Enter on an integer asks App for a one-line prompt; Left and Right step an integer by one within its range. Enter on the `keymap` row asks App to open the [Key Bindings editor](./keymap_view.hpp.skel.md#class-keymapview), and on the `colors` row the [Colors editor](./colors_view.hpp.skel.md#class-colorsview); the `colors` row shows how many entries are changed, as the `keymap` row does. Esc closes. Every other key is consumed. The frame and the key hints on the status line are drawn by App.

The panel never changes [Settings](../app/settings.hpp.skel.md#class-settings) itself: `handle_key` reports what the user asked for and App applies it, so saving, applying to the session and refreshing the menu stay in one place.

- **Owns:** the selection and the scroll position.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** optional — the settings still load and apply without the panel.
- **Failure modes:** an area too short for the list scrolls it to keep the selected row visible; below 4 rows the help text is not drawn.
- **Depends on:** [setting_specs](../app/settings.hpp.skel.md#function-setting_specs)
- **Depends on:** [Settings.value](../app/settings.hpp.skel.md#function-value)
- **Depends on:** [KeyEvent](./input.hpp.skel.md#symbol-keyevent)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Unknowns:** none

## symbol: SettingsKeyResult

`{ bool closed; const SettingSpec* change; std::int64_t value; const SettingSpec* edit; }`. `closed`: Esc closed the panel. `change` with `value`: set this setting to this value. `edit`: open a prompt for this integer's value, or the editor of a structured row. At most one of the three is set.

- **Access:** public.

## class: SettingsView

- **Inputs:** none at construction.
- **State changes:** `closed → open → closed`. While open it holds a pointer to the `Settings` given to `open` and reads the current values from it at every draw, so a change App has just applied shows in the next frame.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [settings_view (implementation)](./settings_view.cpp.skel.md)
- **Referred by:** [settings_view_test](../../tests/settings_view_test.cpp.skel.md)

### function: open

- **Inputs:** a `const Settings&` that outlives the open panel.
- **Returns:** nothing.
- **State changes:** opens the panel with the first row selected. `close()` closes it; `is_open()` reports the state.
- **Access:** App (the `UserSettings` command).

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** a [SettingsKeyResult](#symbol-settingskeyresult).
- **State changes:** moves the selection, or closes the panel on Esc. A step that would leave an integer's range, and Space on an integer, report nothing.
- **Access:** App, while the panel has focus.

### function: render

- **Inputs:** a `Screen&`; `area`: the rectangle inside the frame.
- **Returns:** nothing.
- **State changes:** draws the rows and the selected row's help text inside `area` only, scrolling the list to keep the selected row visible.
- **Access:** App.render.

### function: row_text

- **Inputs:** a row index.
- **Returns:** the row as drawn, without the selection marker: the label, padding to a common value column, then the value. `row_count()` is the number of rows and `selected()` the selected index.
- **State changes:** none.
- **Access:** `render` and tests.
