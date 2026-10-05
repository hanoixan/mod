# Search and replace

## Find

Ctrl+F opens the find bar at the bottom, filled with the selection or the last search. mod searches as you type and selects the match.

- Enter finds the next match, Shift+Enter the previous one. F3 and Shift+F3 do the same with the bar closed.
- Alt+R switches regular expressions on and off, Alt+C ignores case, and Alt+W matches whole words only. The bar shows which are on.
- Esc closes the bar.

Search works on files of any size: it reads the file in pieces and never copies it whole.

## Replace

Tab in the find bar adds a Replace field.

- Enter replaces the selected match and finds the next one.
- Alt+A replaces every match at once, as one step you can undo.
- With regular expressions on, `$1` and the like in the replacement refer to the groups of the match.

In [read-only mode](read-only.md) only Find is available.

## Go to Line

Ctrl+G asks for a line number and moves there.
