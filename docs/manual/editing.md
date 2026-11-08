# Editing

## Moving and selecting

Arrows move by character and line, Home and End to the start and end of the line, PageUp and PageDown by a screen, and Ctrl+Home and Ctrl+End to the start and end of the file. Ctrl+Left and Ctrl+Right move by word. Ctrl+A and Ctrl+E also go to the start and end of the line.

Ctrl+Up and Ctrl+Down scroll the view a row at a time without moving the cursor, as in VS Code: the text and the cursor move together, and the cursor may scroll out of view. Scrolling stops when the first line is at the top or the last line at the bottom. The next key that moves the cursor or edits brings the view back to it, with up to five rows of text showing beyond it on the side it was scrolled off.

Hold Shift with any of these to select. Typing replaces the selection.

The cursor never stops inside a character made of several code points, such as an emoji with a skin tone or a letter with an accent, and never between the two halves of a Windows line ending.

## The mouse

With the [`mouse` setting](settings.md#the-settings) on (it is unless you turn it off; `--no-mouse` for one session), mod takes the mouse:

- A **click** puts the cursor where you click, and gives the focus to the [split view](documents.md#split-views) you click in. Clicking in the line numbers puts it at the start of that line.
- A **drag** selects from where you pressed to where you let go; dragging past the top or bottom of the view scrolls it.
- A **double click** selects a word (or a run of spaces or punctuation), a **triple click** the whole line; keep the button down and drag to select more whole words or lines.
- **Shift+click** extends the selection to where you click.
- The **wheel** scrolls the view under the pointer three rows a notch without moving the cursor, as Ctrl+Up and Ctrl+Down do.
- With the menu bar shown (Esc), click a menu to open it and an item to run it; a click anywhere else closes the menu. The [file dialog](file-dialog.md) and the [folder tree](folder-tree.md) take clicks too.

The selection is mod's own, so Ctrl+C, Ctrl+X, typing and Delete act on it. While mod has the mouse your terminal's own selection, for copying text from the screen, needs Shift held as you drag in most terminals (Windows Terminal, GNOME Terminal, Konsole, xterm, kitty, Alacritty; Option in iTerm2). mod's Copy reaches your desktop's clipboard only in terminals that support OSC 52 (see below); GNOME Terminal and Konsole do not, so use Shift and the terminal's own copy there. Turn the setting off to leave the mouse entirely to the terminal.

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
