<# Settings

Options > User Settings… lists every setting. Up and Down move, Enter or Space turns an on/off setting on or off, Enter on a number asks for a value, Left and Right step a number, Enter, Space, Left and Right step through a choice, and Esc closes. Each change applies at once, except the terminal mode, which applies the next time mod starts, and is remembered. A setting can also be given on the [command line](command-line.md#settings-for-one-session) for one session, without being saved. The five settings changed most recently also appear in the Options menu for quick changes.

## The settings

| Setting | Default | Meaning |
|---|---|---|
| `tab_width` | 4 | Columns between tab stops, 1 to 16: how wide a tab looks, and how far Tab and Shift+Tab indent with spaces. |
| `line_numbers` | on | Show line numbers in each document as it is opened. Changing it also sets every open document. |
| `syntax_coloring` | on | Color code and style Markdown in each document as it is opened (changing it also sets every open document): keywords, strings and comments, a [language server](language-servers.md)'s names, and Markdown's headings, emphasis and links. |
| `word_wrap` | on | Wrap long lines in each document as it is opened, instead of scrolling sideways. Changing it also sets every open document. |
| `darkness` | normal | How dark the screen is: `normal` uses reverse video for the bars and markers, `night` bright bold text instead, `paper` draws the text on a light page with plain bars. See [Colors](colors.md#darkness). |
| `read_only_copy` | markdown | What Copy takes from a Markdown file shown laid out in [read-only mode](read-only.md): `markdown` copies the source, marks and all; `visible text` copies the text as you see it. |
| `tab_inserts` | spaces | What Tab inserts: `spaces` up to the next tab stop, or a `tab` character. Shift+Tab removes up to a tab width of leading spaces, or a leading tab, from the line, or from every line of a selection. |
| `cursor_style` | bar | The text cursor's shape: `bar` (a vertical line), `block` or `underline`, each also as `-blink` (`bar-blink`…). Some terminals ignore it; vt100 mode leaves the terminal's own cursor. Applies at once; mod restores the terminal's own shape when it exits. |
| `terminal_mode` | auto | How mod writes to the terminal: `auto` detects it, `vt100` uses only VT100 codes (bold, underline and reverse, no colors, no alternate screen), `xterm` uses colors and modern features. A choice: Enter, Space or Left and Right step through the names. Applies the next time mod starts. |
| `keymap` | none changed | Your [key bindings](key-bindings.md), edited with Options > Key Bindings…. |
| `colors` | none changed | Your [colors](colors.md), edited with Options > Colors…. |

The View menu's check marks change only the document you are in; `line_numbers`, `syntax_coloring` and `word_wrap` choose how each document starts when it is opened, and changing one of them also sets every open document.

## settings.json

Settings are kept in `settings.json` in your configuration folder: `$XDG_CONFIG_HOME/mod/` if that is set, otherwise `~/.config/mod/`. mod writes it whenever you change a setting, and you can edit it by hand while mod is not running. A value mod does not understand is ignored with a message, and the setting keeps its default.

A `modified` member records when each setting was last changed; that is how the Options menu knows the recent ones. Members mod does not know are kept as they are.

A file listing every setting at its default (all but `colors`, which has no entries by default) is installed with mod as `share/mod/settings.json` (for example `/usr/local/share/mod/settings.json`), to copy and edit. mod itself does not read that copy.

```json
{
  "tab_width": 2,
  "word_wrap": false,
  "keymap": { "GotoLine": ["Ctrl+G", "F5"] },
  "colors": { "keyword": "bold magenta", "comment": "dim" }
}
```
