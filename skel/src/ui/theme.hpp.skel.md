---
role: product
stamp: source 1a669e53, stand-in 50274114
---
# module: theme

The one place that decides how each [Style](../syntax/highlight.hpp.skel.md#symbol-style) and each token modifier looks. Highlighters and views never pick colors themselves: they ask `attr_for`.

Every look is written as a **color spec**: words separated by spaces, using only the 16 ANSI colors, so the user's terminal palette (light or dark) still decides the actual shades.

| Word | Meaning |
|---|---|
| `black` `red` `green` `yellow` `blue` `magenta` `cyan` `white` | the foreground color |
| `bright-<color>` | the bright foreground color (`bright-cyan`) |
| `on-<color>`, `on-bright-<color>` | the background color |
| `bold` `dim` `italic` `underline` `reverse` `strike` | attributes |
| `bright` | in a modifier's spec only: make the foreground bright |
| `plain` | the terminal's own colors and no attributes (only on its own) |

Words are matched without regard to case, in any order; a later color word replaces an earlier one. Anything else makes the spec invalid.

Each style and each modifier has a **name** (camelCase, the LSP's own names where there is one) and a **default spec**. The user may override any of them with the `colors` [setting](../app/settings.hpp.skel.md#function-setting_specs); the overrides are kept in the **active theme**, which `attr_for` reads.

| Name | Style | Default |
|---|---|---|
| `text` | `Default` | `plain` |
| `markdownHeading1` | `md_heading1` | `bold bright-cyan` |
| `markdownHeading2` | `md_heading2` | `bold cyan` |
| `markdownHeading3` … `markdownHeading6` | `md_heading3` … `md_heading6` | `bold bright-blue` |
| `markdownEmphasis` | `md_emphasis` | `italic` |
| `markdownStrong` | `md_strong` | `bold` |
| `markdownStrike` | `md_strike` | `strike` |
| `markdownCode`, `markdownCodeBlock` | `md_code`, `md_code_block` | `green` |
| `markdownLinkText` | `md_link_text` | `underline bright-blue` |
| `markdownLinkUrl` | `md_link_url` | `bright-blue` |
| `markdownQuote` | `md_quote` | `italic dim` |
| `markdownListMarker` | `md_list_marker` | `bold yellow` |
| `markdownMarkup` | `md_markup` | `dim` |
| `namespace` `type` `class` `enum` `interface` `struct` `typeParameter` | the `lsp_` styles | `cyan` |
| `parameter` | `lsp_parameter` | `italic bright-cyan` |
| `variable` | `lsp_variable` | `bright-cyan` |
| `property` | `lsp_property` | `bright-blue` |
| `enumMember` | `lsp_enum_member` | `bright-blue` |
| `event` `function` `method` | the `lsp_` styles | `bright-blue` |
| `macro` | `lsp_macro` | `bright-magenta` |
| `keyword` `modifier` | the `lsp_` styles | `magenta` |
| `comment` | `lsp_comment` | `dim italic` |
| `string` | `lsp_string` | `green` |
| `number` | `lsp_number` | `yellow` |
| `constant` | `constant` | `yellow` |
| `regexp` | `lsp_regexp` | `red` |
| `operator` | `lsp_operator` | `plain` |
| `decorator` | `lsp_decorator` | `yellow` |
| `searchMatch` | `search_match` | `black on-yellow` |
| `selection` | `selection` | `reverse` |
| `gutter` | `gutter` | `dim` |
| `gutterCurrent` | `gutter_current` | `bold` |
| `status` | `status` | `black on-bright-white` |
| `statusUnfocused` | `status_unfocused` | `white on-bright-black` |
| `menu` | `menu` | `black on-bright-white` |
| `menuSelected` | `menu_selected` | `bold black on-bright-blue` |
| `menuAccel` | `menu_accel` | `underline black on-bright-white` |
| `error` | `error` | `bold bright-white on-red` |
| `historyReadOnly` | `history_read_only` | `dim` |
| `overflowMarker` | `overflow_marker` | `black on-bright-white` |
| `listSelected` | `list_selected` | `bold black on-bright-blue` |
| `listSelectedUnfocused` | `list_selected_unfocused` | `reverse` |
| `page` | `page` | `plain` |
| `historyInserted` | `history_inserted` | `black on-green` |
| `historyRemoved` | `history_removed` | `strike red` |

Modifiers are applied on top of the style's look in this order, each adding its attributes and replacing the color only when its spec names one:

| Name | Bit | Default |
|---|---|---|
| `declaration` | `kModDeclaration` | `bold` |
| `readonly` | `kModReadonly` | `bright` |
| `defaultLibrary` | `kModDefaultLibrary` | `italic` |
| `deprecated` | `kModDeprecated` | `strike` |
| `documentation` | `kModDocumentation` | `italic` |

**In vt100 mode** there is no color, dim, italic or strike, so the theme answers from a fixed fallback table instead, and color overrides do not apply: keywords, modifiers, macros, Markdown headings and strong text, `gutterCurrent` and the `declaration` modifier are bold; comments, Markdown emphasis, quotes and links are underlined; strike (Markdown, `deprecated` and `historyRemoved`) becomes reverse; `historyInserted` is underlined; the bars and highlights stand out from plain text: `selection`, `status`, `menu`, `overflowMarker`, `searchMatch` and `listSelected` are reverse, `menuAccel` reverse underline, `statusUnfocused` bold, `listSelectedUnfocused` underlined, and `menuSelected` plain (cut out of the reverse menu); `error` is bold reverse; everything else, color-only text included, is plain. The vt100 look does not follow darkness: it is the same in normal, night and paper, the page included.

**Darkness** (`enum class Darkness { night, normal, paper }`, the `darkness` [setting](../app/settings.hpp.skel.md#function-setting_specs)) changes the defaults of the page and the blues (the bars are the same in every look) (no default uses dark blue on a dark screen; paper's light page keeps it), and only the defaults: an override still wins, and a spec equal to the level's default is no override.

| Name | normal | night | paper |
|---|---|---|---|
| `page` | `plain` | `plain` | `black on-bright-white` |
| `event`, `function`, `method`, `markdownLinkUrl` | `bright-blue` | `bright-blue` | `blue` |
| `markdownLinkText` | `underline bright-blue` | `underline bright-blue` | `underline blue` |
| `menuSelected`, `listSelected` | `bold black on-bright-blue` | `bold black on-bright-blue` | `bold bright-white on-blue` |

`page` is the background the text area is drawn on: EditorView and the help viewer lay each of their looks over it with `on_page` (its colors fill in a look's default colors, and its reverse inverts the look, so a selection still shows). The overflow marker is not laid over it, so in paper it stands out as the terminal's own colors. In vt100 mode the page is plain whatever the darkness.

`selection` and `searchMatch` are overlays: EditorView combines them with the text's own look as before (the selection adds its attributes; a search match replaces the look). Apart from `variable`, `property`, `parameter`, the new `constant` and the two new modifiers, every default is the look the fixed table gave before.

- **Owns:** the default table, the active theme's overrides.
- **Access:** public. The active theme is one instance for the process, main thread only.
- **Required:** always.
- **Failure modes:** low contrast on some palettes, for example bright yellow on a light background; the user can override any entry. An invalid spec in settings.json is skipped with a warning and the default kept.
- **Depends on:** [Style](../syntax/highlight.hpp.skel.md#symbol-style)
- **Depends on:** [Attr](./screen.hpp.skel.md#symbol-attr)
- **Depends on:** [Json](../syntax/json.hpp.skel.md#class-json)
- **Unknowns:** none

## function: parse_color_spec

- **Inputs:** `text`: a color spec; `for_modifier`: whether `bright` is allowed.
- **Returns:** `Result<ColorSpec>`, where `ColorSpec` is `{ std::optional<uint8_t> fg; std::optional<uint8_t> bg; uint8_t flags; bool brighten; }`; `format` naming the offending word for an invalid one (an empty spec is invalid).
- **State changes:** none.
- **Access:** ColorTheme, App (the Colors editor's prompt) and tests.
- **Referred by:** [fuzz_color_spec](../../fuzz/fuzz_color_spec.cpp.skel.md)

## function: color_names

- **Inputs:** none.
- **Returns:** every style and modifier name in the tables' order, as `std::span<const ColorEntry>` where `ColorEntry` is `{ std::string_view name; std::optional<Style> style; uint8_t modifier_bit; std::string_view default_spec; std::string_view group; }` (`group`: "Text", "Markdown", "Modifiers" or "Interface", for the editor's headings). Exactly one of `style` and `modifier_bit` is set.
- **State changes:** none.
- **Access:** ColorTheme, ColorsView, tests.
- **Referred by:** [colors.md](../../docs/manual/colors.md.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)

## class: ColorTheme

- **Inputs:** none; it starts with no overrides.
- **State changes:** the overrides, as name → spec text. Invariant: every override names an entry of `color_names` and parses.
- **Owns:** the overrides.
- **Access:** App owns the active one, reached through `active_theme()`; tests make their own.
- **Referred by:** [colors_view](./colors_view.hpp.skel.md)
- **Referred by:** [color_theme_test](../../tests/color_theme_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [theme (implementation)](./theme.cpp.skel.md)
- **Referred by:** [colors_view_test](../../tests/colors_view_test.cpp.skel.md)
- **Referred by:** [cli_options](../app/cli_options.hpp.skel.md)
- **Referred by:** [fuzz_config](../../fuzz/fuzz_config.cpp.skel.md)
- **Referred by:** [startup](../app/startup.hpp.skel.md)

### function: apply

- **Inputs:** `colors`: the `colors` member of settings.json (null when absent).
- **Returns:** one warning per skipped entry: "colors must be an object", "unknown color name <name>", "<name>: <reason>".
- **State changes:** replaces all overrides with the valid entries.
- **Access:** App at startup.

### function: set

- **Inputs:** a name and a spec text.
- **Returns:** `Status`: `format` for an unknown name or an invalid spec, with nothing changed.
- **State changes:** overrides the entry; a spec equal to the default removes the override instead. `reset(name)` removes one override, `reset_all()` every one.
- **Access:** App, from the Colors editor.

### function: set_session

- **Inputs:** `name`, `spec`: as for `set`.
- **Returns:** `Status`, as `set`.
- **State changes:** a session color for `name`, drawn over its saved override and never part of `overrides` (so never saved). `set` and `reset` of that name, and `reset_all`, drop it: a change made in the Colors editor replaces the command line's.
- **Access:** App, for the command line's `--color` flags; [parse_cli](../app/cli_options.hpp.skel.md#function-parse_cli) validates with it on a scratch theme.

### function: spec_of

- **Inputs:** a name.
- **Returns:** its spec text in effect (the override, else the default). `is_default(name)` says whether it is not overridden, and `overrides()` returns the overrides as a JSON object (empty when none), for settings.json.
- **State changes:** none.
- **Access:** ColorsView, App.

### function: attr

- **Inputs:** a `Style`; `modifiers`: the modifier bits. `set_vt100(bool)` switches to the vt100 fallback table described above; `set_darkness(Darkness)` chooses the defaults by darkness; `on_page(Attr)` lays a look over the page (and the free `on_page` does so in the active theme).
- **Returns:** the `Attr`: the style's spec (fg, bg and flags; unset colors are the terminal default), then each set modifier's spec merged in the table's order.
- **State changes:** none. Looks are cached per style and recomputed only after a change.
- **Access:** `attr_for`.

**The unfocused status line.** `unfocused_status()` is the look of a split's status line while another split has the focus, and of that split's overflow markers: the `statusUnfocused` entry (white on dark grey by default, in every darkness), set like any other color.

## function: attr_for

- **Inputs:** `style`; `modifiers`, default 0.
- **Returns:** `active_theme().attr(style, modifiers)`. With no overrides, exactly the defaults above.
- **State changes:** none.
- **Access:** [EditorView](./editor_view.hpp.skel.md#class-editorview), [MenuBar](./menu.hpp.skel.md#class-menubar), [Prompt](./prompt.hpp.skel.md#class-prompt) and every other view.
- **Referred by:** [editor_view](./editor_view.hpp.skel.md)
- **Referred by:** [menu](./menu.hpp.skel.md)
- **Referred by:** [prompt](./prompt.hpp.skel.md)
- **Referred by:** [history_view](./history_view.hpp.skel.md)
- **Referred by:** [keymap_view](./keymap_view.hpp.skel.md)
- **Referred by:** [settings_view](./settings_view.hpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)
- **Referred by:** [keymap_view_test](../../tests/keymap_view_test.cpp.skel.md)
- **Referred by:** [settings_view_test](../../tests/settings_view_test.cpp.skel.md)
- **Referred by:** [doc_search_view](./doc_search_view.hpp.skel.md)
- **Referred by:** [help_viewer](./help_viewer.hpp.skel.md)
- **Referred by:** [confirm_bar](./confirm_bar.hpp.skel.md)
- **Referred by:** [file_dialog](./file_dialog.hpp.skel.md)
