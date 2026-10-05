# Getting started

## Starting

```text
mod                       an empty, untitled buffer
mod notes.md              one file
mod a.txt b.md c.cpp      several files, each a document; the first is shown
mod -ro notes.md          read-only
mod --help                usage, and every option
mod --version             the version
```

Options can also open files [read-only or with their history saved](command-line.md), and set any setting for one session.

A path that does not exist yet opens as a new, empty file; it is created when you save. A path that cannot be opened (a folder, a file you may not read) is skipped; the first such failure is shown on the status line.

## The screen

- The text fills the screen above the status line. The menu bar (File, Edit, View, Documents, Options, Help) is hidden until Esc (or F10, or Alt+X) shows it on the screen's top line, pushing the text down a row; then a menu's underlined letter opens it, below the bar. Esc three times in quick succession (each within a quarter of a second of the last) quits. See [Menus](menus.md).
- The text has line numbers on the left. Long lines wrap onto the next rows; with View > Word Wrap off, they scroll sideways instead and end in a highlighted `>` while they run past the right edge.
- The bottom row is the status line, starting with `>` to mark the view you are in. While a prompt (the find bar, Go to Line, a file name) or a question (such as Save, Discard or Cancel) is open, it takes the screen's bottom lines and the status line moves up above it. On the left, the file name, `*` when there are unsaved changes, `[read-only]` for a file you may not write, and `[view]` in [read-only mode](read-only.md); in the center, messages, and `Esc h: help` when there are none; on the right, the cursor's line and column and the number of lines. While the file dialog, the Undo History pane, a settings panel or the help fills the screen, the bottom line is its own: its keys and messages, without a document's name.

## Saving and quitting

- Ctrl+S saves. An untitled buffer asks for a name first, in the [file dialog](file-dialog.md). File > Save As… saves under another name.
- Ctrl+Q quits. For each document with unsaved changes, mod asks Save, Discard or Cancel; Cancel stays in mod.
- Saving replaces the file safely: mod writes a new copy and swaps it in, so a crash never leaves a half-written file. When that is impossible, mod asks before writing over the file in place, which is not crash-safe.

## Files next to yours

mod keeps each file's undo history in memory. If you check Persist History in the Undo History pane, it is also kept in a file beside yours named after it with `.mod` added, such as `notes.md.mod`, which holds every change you ever made, including deleted text, until you clear or trim it; see [Undo history](undo-history.md).

Your settings live in `~/.config/mod/settings.json` (or `$XDG_CONFIG_HOME/mod/settings.json`); see [Settings](settings.md).
