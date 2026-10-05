---
role: product
stamp: source 1e4046b1, stand-in 9b0eb0f5
---
# module: settings

User settings that `mod` remembers between sessions, and their **schema**. The schema is one table in the code, [setting_specs](#function-setting_specs): a row per setting with its key, type, range, default, label and help text. Everything that touches settings is driven by that table: `load` validates each row's value, `set` range-checks and writes it, the [SettingsView](../ui/settings_view.hpp.skel.md#class-settingsview) panel (Options > User Settings…) draws one line per row, and the Options menu lists the rows changed most recently. A new setting is therefore one new table row plus whatever App does with its value; no loader, editor or menu code changes.

The settings today:

| Key | Type | Default | Meaning |
|---|---|---|---|
| `tab_width` | integer, 1 to 16 | 4 | Columns between tab stops. |
| `line_numbers` | boolean | true | "Line numbers on open": a document's View > Line Numbers when it is opened. |
| `syntax_coloring` | boolean | true | "Syntax coloring on open": a document's View > Syntax Coloring, which also styles Markdown, when it is opened. |
| `pin_folder_tree` | boolean | false | "Pin Folder Tree": whether the folder tree starts pinned (kept shown beside the views; unpinned it shows only while it has the keys); changing it pins or unpins it now. View > Pin Folder Tree changes it for the session. |
| `word_wrap` | boolean | true | "Word wrap on open": a document's View > Word Wrap when it is opened (on by default, as in ../modi/). |
| `read_only_copy` | choice: `markdown`, `visible text` | `markdown` | "Copy in read-only Markdown": what Copy takes from a Markdown document laid out for reading — the source, marks and all, or the text as shown. |
| `tab_inserts` | choice: `spaces`, `tab` | `spaces` | What `InsertTab` (Tab) inserts: spaces up to the next tab stop, or a tab character. |
| `keymap` | keymap | none changed | The user's key bindings; see [Keymap](./keymap.hpp.skel.md#class-keymap). |
| `darkness` | choice: `night`, `normal`, `paper` | `normal` | How dark the screen is; see the [theme](../ui/theme.hpp.skel.md). Applies at once. |
| `cursor_style` | choice: `bar`, `bar-blink`, `block`, `block-blink`, `underline`, `underline-blink` | `bar` | The text cursor's shape, sent by the [terminal output](../platform/terminal_output.hpp.skel.md#function-cursor_shape). Applies at once. |
| `terminal_mode` | choice: `auto`, `vt100`, `xterm` | `auto` | How mod writes to the terminal; see [terminal_output](../platform/terminal_output.hpp.skel.md). Applies when mod next starts. |
| `colors` | colors | none changed | The user's color overrides, name → [color spec](../ui/theme.hpp.skel.md); edited in Options > Colors…. |

The View menu toggles are per-session: they start from these settings and flipping one in the View menu is never written. Changing the setting itself (in the panel or from the Options menu) is written and also applied to the running session.

Settings are **global**: one value for every file and file type, kept in `settings.json` in the user configuration directory (for example `~/.config/mod/settings.json`). A hand-edited file is read as long as it is valid. The file also records **when each setting was last changed**, in a `modified` object, so the Options menu can offer the most recent ones.

- **Owns:** the schema table, the in-memory copy of the settings, and every read and write of [settings_file](../../infra/storage.iac.skel.md#resource-settings_file).
- **Access:** public. One instance, owned by [App](./app.hpp.skel.md#class-app). Main thread only.
- **Required:** optional — without it every setting starts at its default in every session.
- **Failure modes:** listed per function. No settings failure ever stops `mod` from starting or editing; the worst case is default values and a status-line message.
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Unknowns:** none

## symbol: kDefaultTabWidth

`inline constexpr int kDefaultTabWidth = 4;` and the accepted range `kMinTabWidth = 1`, `kMaxTabWidth = 16`, used by the `tab_width` row of the schema.

- **Access:** public.

## symbol: SettingType

`enum class SettingType { boolean, integer, keymap }`: what kind of value a schema row holds. See [SettingSpec](#symbol-settingspec).

- **Access:** public.

## symbol: SettingSpec

`{ std::string_view key; SettingType type; std::int64_t def, min, max; std::string_view label; std::string_view help; std::span<const std::string_view> choices; }`, with `enum class SettingType { boolean, integer, choice, keymap, colors }`. A **choice** is one of the names in `choices`, held as its index (`min` 0, `max` the last index) and stored in the file as the name; a name not among them is a bad value. A `keymap` or `colors` row is a **structured** value with an editor of its own: it has no scalar value (`value` is 0, `set` refuses it, it is never among the recent settings), `load` leaves its validation to its owner, and it is read and written through `raw` and `set_raw`. It is in the schema so that the User Settings panel lists it, which keeps every user setting reachable from that one panel. A boolean is held as 0 or 1 with `min == 0` and `max == 1`, and is stored in the file as a JSON boolean; an integer is stored as a JSON integer. `label` is the row's name in the panel and the menu; `help` is one or two sentences shown under the list for the selected row.

- **Access:** public.
- **Referred by:** [settings_view](../ui/settings_view.hpp.skel.md)
- **Referred by:** [menu](../ui/menu.hpp.skel.md)

## function: setting_specs

- **Inputs:** none.
- **Returns:** the whole schema as `std::span<const SettingSpec>`, in display order. Keys are unique.
- **State changes:** none. The table is a compile-time constant.
- **Access:** Settings, SettingsView, MenuBar, App and tests.
- **Referred by:** [settings.md](../../docs/manual/settings.md.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
- **Referred by:** [cli_options](./cli_options.hpp.skel.md)

## function: find_setting

- **Inputs:** `key`: a setting's key.
- **Returns:** a pointer to the schema row with that key, or null when the schema has none. The pointer stays valid for the life of the process.
- **State changes:** none.
- **Access:** App and tests.

## function: setting_value_text

- **Inputs:** a `SettingSpec` and a value of it.
- **Returns:** the value's display text: `on` / `off` for a boolean, the number for an integer, the name for a choice, empty for a structured row.
- **State changes:** none.
- **Access:** App (the status line after a change) and SettingsView.

## function: default_settings_json

- **Inputs:** none.
- **Returns:** `settings.json` with every setting at its default: one member per scalar row of the schema in schema order (booleans as `true`/`false`), then `"keymap": {}`; two-space indent, one member per line, ending in a newline.
- **State changes:** none.
- **Access:** [gen_default_settings](../../tools/gen_default_settings.cpp.skel.md) and tests.
- **Referred by:** [settings](../../config/settings.json.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
- **Referred by:** [gen_default_settings](../../tools/gen_default_settings.cpp.skel.md)

## class: Settings

- **Inputs:** `config_dir`: the result of [user_config_dir](../platform/fs.hpp.skel.md#function-user_config_dir) (a `Result<std::filesystem::path>`), injected so that tests can point it at a scratch directory; an error there means settings are neither read nor written for this session. `write`: an optional file-operations seam, `std::function<Status(const std::filesystem::path&, const ContentProducer&)>`, that replaces the call to `write_atomically`; empty (the default) uses `write_atomically` itself. Tests use it to see whether a write happened. `now_ms`: an optional clock returning Unix milliseconds, for the modified times; empty uses the system clock.
- **State changes:** every value is always within its row's range. The parsed JSON object is kept, so keys this version does not know (at the top level and inside `modified`) are written back unchanged.
- **Owns:** see the module.
- **Access:** App.
- **Depends on:** [Json](../syntax/json.hpp.skel.md#class-json)
- **Depends on:** [user_config_dir](../platform/fs.hpp.skel.md#function-user_config_dir)
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [settings (implementation)](./settings.cpp.skel.md)
- **Referred by:** [settings_test](../../tests/settings_test.cpp.skel.md)
- **Referred by:** [settings_view_test](../../tests/settings_view_test.cpp.skel.md)
- **Referred by:** [menu_test](../../tests/menu_test.cpp.skel.md)
- **Referred by:** [startup](./startup.hpp.skel.md)

### function: load

- **Inputs:** none. Ambient: the settings file.
- **Returns:** an optional warning message for the status line.
- **State changes:** resets every setting to its default, then reads `<config_dir>/settings.json` if it exists and takes each schema key's value from it. A missing directory or file is normal: the defaults are used and there is no warning. A file that cannot be used at all (unreadable bytes, invalid JSON, a top-level value that is not an object, larger than 1 MiB) leaves every default in place, and the warning is "settings.json ignored: <reason>". A single value of the wrong type or out of range costs only that setting: it keeps its default, the others still load, and the warning is "settings.json: <key> must be <what>", with "(and N more bad values)" when there are several. The modified times are read from the `modified` object; a time that is not an ISO 8601 UTC string in the written form (with or without milliseconds) is ignored, and so is a `modified` that is not an object. The file is left as it is until the next `set` rewrites it.
- **Access:** App, once during `starting`, before EditorView and Editor are built.
- **Depends on:** [settings_file](../../infra/storage.iac.skel.md#resource-settings_file)
- **Depends on:** [Json.parse](../syntax/json.hpp.skel.md#function-parse)

### function: value

- **Inputs:** `key`: a schema key.
- **Returns:** the setting's value as `std::int64_t` (0 or 1 for a boolean); 0 for a key that is not in the schema. `flag(key)` is `value(key) != 0`, and `tab_width()` is `value("tab_width")` as an `int`.
- **State changes:** none.
- **Access:** App, SettingsView, MenuBar.

### function: darkness

The **typed reads** of the choice settings, each enum listing the setting's choices in their order: `darkness()` (`Darkness`, from the theme), `cursor_style()` (`CursorStyle`, from terminal_output), `tab_inserts()` (`TabInserts { spaces, tab }`), `read_only_copy()` (`ReadOnlyCopy { markdown, visible_text }`), and `terminal_mode()` (a `TerminalMode`, or nullopt for auto). App reads choices only through these, never as raw numbers.

- **Inputs:** none.
- **Returns:** the setting's current value (an override if there is one) as its type.
- **State changes:** none.
- **Access:** App.

### function: override

- **Inputs:** `key`: a scalar schema key; `value`: within the row's range.
- **Returns:** `Status`: `internal` for an unknown key, a structured row or a value out of range, with nothing changed.
- **State changes:** the session's value of the setting, which `value` returns; nothing is written and the modified time is not stamped, so the override never reaches `settings.json`. A later `set` of the same key replaces it and is saved as usual; a `set` of another key writes only that key (the file is written from what was loaded and set, never from the overrides).
- **Access:** App, for the command line's setting flags.

### function: set

- **Inputs:** `key`: a schema key; `value`: within the row's range (the panel and the prompt have already validated it).
- **Returns:** `Status`. `internal` for an unknown key, a structured row or a value out of range, with nothing changed. `permission`, `no_space` or `io` when the file cannot be written; the in-memory value is still updated, so it applies for this session.
- **State changes:** updates the value and stamps the setting's modified time with the current time, as `YYYY-MM-DDTHH:MM:SS.mmmZ` (UTC; a form that sorts as text); creates `config_dir` (and missing parents) if needed; writes the whole object (known and unknown keys, and `modified`) with [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically); a new file gets that function's new-file permission bits. Nothing is written and nothing is stamped when the value is unchanged.
- **Access:** App, after a change in the User Settings panel, a setting prompt, or a recent-settings item of the Options menu.
- **Failure modes:** two `mod` processes change settings: each write first re-reads the file and changes only its own member over it, so both changes are kept (each session keeps its own values in memory). Two writes in the same instant can still race; there is no lock. The file is written through a symlink to the real file (a dotfile manager's link stays a link), keeping that file's permissions.
- **Depends on:** [settings_file](../../infra/storage.iac.skel.md#resource-settings_file)
- **Depends on:** [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically)
- **Depends on:** [Json.dump](../syntax/json.hpp.skel.md#function-dump)

### function: recent

- **Inputs:** `max`: the most rows to return, 5 by default.
- **Returns:** the schema rows whose settings have a modified time, newest first, at most `max`. Settings stamped in the same millisecond keep the schema order.
- **State changes:** none.
- **Access:** MenuBar (the Options menu's recent-settings items) and App (to run one).

### function: raw

- **Inputs:** `key`: a member name of the file.
- **Returns:** a pointer to that member's JSON value as loaded or last set, or null when it is absent.
- **State changes:** none.
- **Access:** App (the `keymap` member, handed to [Keymap.apply_overrides](./keymap.hpp.skel.md#function-apply_overrides)) and SettingsView (to count the changed commands).

### function: set_raw

- **Inputs:** `key`; `value`: the new JSON value, or `nullopt` to remove the member.
- **Returns:** `Status`, with the file errors of `set`.
- **State changes:** replaces or removes the member and writes the file as `set` does. Nothing is written when the value is unchanged (or already absent). No modified time is stamped: a structured member never appears among the recent settings.
- **Access:** App, after every change in the Key Bindings editor.
- **Depends on:** [write_atomically](../platform/fs.hpp.skel.md#function-write_atomically)
