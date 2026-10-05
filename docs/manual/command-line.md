# The command line

```text
mod [OPTION...] [PATH...]
```

Each path opens as a document; the first is shown. Without any, mod starts with an untitled buffer. `mod --help` lists every option, and `mod --version` prints the version.

## Opening files

| Option | Effect |
|---|---|
| `-ro`, `--read-only` | Opens the named files in [read-only mode](read-only.md), as if View > Read Only were chosen in each. Files you open later open as usual. |
| `--persist-history` | Turns on Persist History for the named files, so their [undo history](undo-history.md) is kept in a `.mod` file beside each. The history file is written from the first change on; if one is there that mod cannot read, mod asks before replacing it. |
| `--` | Ends the options: what follows is a path, even if it starts with `-`. |

## Settings for one session

Every setting in the [settings table](settings.md#the-settings) except the key bindings and colors has an option of the same name, with dashes for underscores (colors have `--color`, below). It sets the value for this session only: it is never saved to `settings.json`. If you change the same setting while mod runs (in User Settings or the Options menu), your change replaces the option's value and is saved as usual.

| Option | Example |
|---|---|
| a number: `--name=N` | `--tab-width=8` |
| on or off: `--name`, `--no-name`, or `--name=on`/`off` | `--no-word-wrap`, `--line-numbers=off` |
| a choice: `--name=CHOICE`, dashes for spaces | `--darkness=night`, `--cursor-style=block-blink`, `--read-only-copy=visible-text` |
| a color: `--color NAME=SPEC`, repeatable | `--color 'keyword=bold red' --color string=green` |

Colors take the names and specs described in [Colors](colors.md). A color given this way is also for this session only; changing it in the Colors editor replaces it and saves your choice.

An unknown option or a value mod cannot use stops mod before it starts, with a message saying what was wrong and the usage.

```text
mod -ro README.md                 read the README laid out, without changing it
mod --persist-history notes.txt   keep notes.txt's undo history between sessions
mod --darkness=paper --tab-width=2 main.c
```
