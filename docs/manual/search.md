# Search and replace

## Find

Ctrl+F opens the find bar at the bottom, filled with the selection or the last search. mod searches as you type and selects the match; every other match on screen is highlighted too (the `searchOther` [color](colors.md)), and the bar counts them: `3 of 17` (`17…` while it is still counting a large file, `10000+` past ten thousand).

- Down or F3 goes to the next match and Up or Shift+F3 to the previous one, without leaving the field, so you can keep typing. Enter and Shift+Enter do the same. F3 and Shift+F3 also work with the bar closed.
- The bar's right side shows the three switches, each with its key: `[ ] Regex Alt+R` searches with a regular expression, `[x] Case Alt+C` matches upper and lower case exactly, `[ ] Word Alt+W` matches whole words only. `[x]` is on. A search starts as plain text that matches case; the switches stay as you leave them for the next search. On a narrow screen they shorten to `[ ]R [x]C [ ]W`. With the [mouse](editing.md#the-mouse) on, click one to flip it.
- Esc closes the bar.

Shift+Enter reaches mod apart from Enter only in terminals that have the kitty keyboard protocol (kitty, WezTerm, foot, Ghostty, Alacritty and others), which mod turns on where it is there. In other terminals Shift+Enter is just Enter: use Up or Shift+F3 to go back.

Search works on files of any size: it reads the file in pieces and never copies it whole.

## Replace

Tab in the find bar adds a Replace field.

- Enter replaces the selected match and finds the next one.
- Alt+A replaces every match at once, as one step you can undo.
- With regular expressions on, `$1` and the like in the replacement refer to the groups of the match.

In [view mode](view-mode.md) only Find is available.

## Go to Line

Ctrl+G asks for a line number and moves there.
