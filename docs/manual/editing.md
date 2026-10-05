# Editing

## Moving and selecting

Arrows move by character and line, Home and End to the start and end of the line, PageUp and PageDown by a screen, and Ctrl+Home and Ctrl+End to the start and end of the file. Ctrl+Left and Ctrl+Right move by word. Ctrl+A and Ctrl+E also go to the start and end of the line.

Hold Shift with any of these to select. Typing replaces the selection.

The cursor never stops inside a character made of several code points, such as an emoji with a skin tone or a letter with an accent, and never between the two halves of a Windows line ending.

## Typing

- Enter inserts the line ending the file already uses (Windows files keep theirs).
- Tab inserts spaces up to the next tab stop, or a tab character when the `tab_inserts` [setting](settings.md) is `tab`; it replaces a selection. Shift+Tab removes one indent (a tab, or up to a tab's width of spaces) from the line, or from each line of the selection. The tab stops, and how wide a tab looks, are the `tab_width` setting.
- Backspace and Delete remove one character; with Ctrl, a word.

## The clipboard

- Ctrl+C copies the selection and Ctrl+X cuts it. Ctrl+V pastes.
- Ctrl+K (Edit > Cut to Line End) cuts from the cursor to the end of the line. At the end of a line it cuts the line break instead, joining the next line on. With a selection it cuts the selection. Pressing it several times in a row collects every piece, so one Ctrl+V afterwards pastes them all.
- Copies of up to 100 KB are also sent to your terminal's own clipboard, for terminals that support it (OSC 52); not in vt100 terminal mode.
- Text pasted from the terminal (your desktop's clipboard) keeps its lines: every line break in it becomes the file's own line ending.

## Undo

Ctrl+Z undoes and Ctrl+Y redoes. Nothing is ever lost by undoing and then typing something else: the old path stays in the history as a branch. See [Undo history](undo-history.md).

## Going to the background

Ctrl+T (File > Suspend) stops mod and returns you to your shell, as Ctrl+Z does for other programs. Type `fg` to come back to it exactly as you left it. On Windows this works only when mod was started from a shell that can continue it, such as MSYS2's bash; started from PowerShell or Windows Terminal, mod says so and keeps running. Ctrl+Z itself is Undo in mod.
