# Key bindings

Every command listed in Options > Key Bindings… can be given keys there. The tables here are the defaults.

## Changing keys

The Key Bindings editor lists every command with its keys.

- Type to search: words match the command's name or its keys, so `go to` and `ctrl+g` both find Go to Line.
- Enter, then press a key: the key is added to the selected command. A key that another command already has is refused; remove it there first.
- Delete removes the selected command's last key.
- Ctrl+R puts the selected command's default keys back; Alt+R does that for every command, after asking.
- A `*` marks a command whose keys you changed.

Only your changes are saved, in the `keymap` member of `settings.json` (see [Settings](settings.md)), so a command you never changed follows mod's defaults even when they change.

Plain typing keys (letters, digits, Shift with them) and Esc cannot be bound. Ctrl+C is Copy and Ctrl+Z is Undo, because mod switches off the terminal's own meaning of those keys.

## Files and the program

| Key | Command |
|---|---|
| `Ctrl+S` | Save |
| `Ctrl+Q` | Exit |
| `Ctrl+T` | Suspend |
| `F1` | Documentation (the [help screen](help.md)) |
| `Alt+X` | Show Menu, as Esc does: then a menu's underlined letter |
| `F10` | Show Menu |

## Editing

| Key | Command |
|---|---|
| `Ctrl+Z` | Undo |
| `Ctrl+Y` | Redo |
| `Ctrl+X` | Cut |
| `Ctrl+K` | Cut to Line End |
| `Ctrl+C` | Copy |
| `Ctrl+V` | Paste |
| `Enter` | Newline |
| `Tab` | Insert Tab: spaces to the next tab stop, or a tab, as the `tab_inserts` setting says |
| `Shift+Tab` | Outdent: remove an indent from the line, or from each line of the selection |
| `Backspace` | Delete Back |
| `Delete` | Delete Forward |
| `Ctrl+Backspace` | Delete Word Back |
| `Ctrl+Delete` | Delete Word Forward |

## Search

| Key | Command |
|---|---|
| `Ctrl+F` | Find/Replace |
| `F3` | Find Next |
| `Shift+F3` | Find Previous |
| `Ctrl+G` | Go to Line |

## Moving

| Key | Command |
|---|---|
| `Left` | Move Left |
| `Right` | Move Right |
| `Up` | Move Up |
| `Down` | Move Down |
| `Ctrl+Left` | Move Word Left |
| `Ctrl+Right` | Move Word Right |
| `Home` | Move Line Start |
| `Ctrl+A` | Move Line Start |
| `End` | Move Line End |
| `Ctrl+E` | Move Line End |
| `PageUp` | Move Page Up |
| `PageDown` | Move Page Down |
| `Ctrl+Home` | Move Doc Start |
| `Ctrl+End` | Move Doc End |

## Selecting

| Key | Command |
|---|---|
| `Shift+Left` | Select Left |
| `Shift+Right` | Select Right |
| `Shift+Up` | Select Up |
| `Shift+Down` | Select Down |
| `Ctrl+Shift+Left` | Select Word Left |
| `Ctrl+Shift+Right` | Select Word Right |
| `Shift+Home` | Select Line Start |
| `Shift+End` | Select Line End |
| `Shift+PageUp` | Select Page Up |
| `Shift+PageDown` | Select Page Down |
| `Ctrl+Shift+Home` | Select Doc Start |
| `Ctrl+Shift+End` | Select Doc End |

## Terminals

Shift and Ctrl with the arrows, Home and End need a terminal that sends xterm's modifier sequences, which almost all do. Some terminals keep F10 for their own menu, and some keep Alt; either one shows mod's menu. The commands Open Menu File, Open Menu Edit and so on open one menu directly; they have no keys, but you can give them some, such as the Alt+F of earlier versions.
