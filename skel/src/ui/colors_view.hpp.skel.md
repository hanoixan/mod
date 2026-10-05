---
role: product
stamp: source 3a01ebf9, stand-in d96c5fa8
---
# module: colors_view

The Colors editor, opened with Options > Colors… (or Enter on the "Colors" row of User Settings). A full-width panel like the Key Bindings editor: one row per [color name](./theme.hpp.skel.md#function-color_names), under the group headings Text, Markdown, Modifiers and Interface. Each row shows the name, a **sample** word drawn in that look (a modifier's sample is drawn as `variable` with the modifier on), the spec text, and `*` when it differs from the default.

- Up, Down, Home, End, PageUp and PageDown move between rows (headings are skipped).
- Enter asks App for a one-line prompt prefilled with the spec; App validates it with [parse_color_spec](./theme.hpp.skel.md#function-parse_color_spec) (the prompt stays open with the error until it parses) and sets it.
- Delete or Ctrl+R gives the selected entry its default again; Alt+R asks App to confirm and then resets every entry. (These are the Key Bindings editor's keys, so the two editors behave alike.)
- Esc closes.

The panel never changes the theme itself; it reports what was asked and App applies and saves it.

- **Owns:** the selection and the scroll position.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** optional — the colors still load from settings.json without the editor.
- **Failure modes:** a look that is unreadable on the user's palette shows in its own row at once, and Delete undoes it.
- **Depends on:** [ColorTheme](./theme.hpp.skel.md#class-colortheme)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Unknowns:** none

## symbol: ColorsKeyResult

`{ bool closed; std::optional<std::string> edit; std::optional<std::string> reset; bool reset_all; }`: Esc closed the panel; `edit`: prompt for this name's spec; `reset`: give this name its default; `reset_all`: confirm, then reset everything.

- **Access:** public.

## class: ColorsView

- **Inputs:** none at construction.
- **State changes:** `closed ⇄ open`; while open it holds the `const ColorTheme&` given to `open`.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [colors_view_test](../../tests/colors_view_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [colors_view (implementation)](./colors_view.cpp.skel.md)

### function: open

- **Inputs:** the theme to show.
- **Returns:** nothing.
- **State changes:** opens with the first entry selected. `close()` and `is_open()` as usual.
- **Access:** App (the `Colors` command).

### function: handle_key

- **Inputs:** a `KeyEvent`.
- **Returns:** a [ColorsKeyResult](#symbol-colorskeyresult).
- **State changes:** moves the selection or closes.
- **Access:** App.

### function: render

- **Inputs:** a `Screen&`; the area inside the frame.
- **Returns:** nothing.
- **State changes:** draws the headings and rows inside the area, scrolled to keep the selection visible, each sample in its look through [ColorTheme.attr](./theme.hpp.skel.md#function-attr).
- **Access:** App.render.

### function: row_text

- **Inputs:** a row index.
- **Returns:** the row as text: a heading, or name, sample word and spec with a trailing `*` when changed. `row_count()`, `selected()` and `selected_name()` expose the rest.
- **State changes:** none.
- **Access:** `render` and tests.
