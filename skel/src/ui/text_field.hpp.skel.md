---
role: product
stamp: source 66e2824c, stand-in 29c23855
---
# module: text_field

A one-line text input: the File name of the file dialog, its filter pattern and the new folder's name. ../modi/ gets these from browser input boxes; in the terminal this class does the editing.

- **Owns:** the text and the cursor.
- **Access:** public. Owned by the dialogs. Main thread.
- **Required:** always.
- **Failure modes:** a pasted text with line breaks: they are dropped, since the field is one line. Text wider than the field scrolls so that the cursor is visible.
- **Depends on:** [KeyEvent](./input.hpp.skel.md#symbol-keyevent)
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [next_grapheme_boundary](../text/utf8.hpp.skel.md#function-next_grapheme_boundary)
- **Depends on:** [prev_grapheme_boundary](../text/utf8.hpp.skel.md#function-prev_grapheme_boundary)
- **Unknowns:** none

## class: TextField

- **Inputs:** `initial`: the starting text; the cursor starts at its end.
- **State changes:** invariants: `0 ≤ cursor ≤ text.size()`, on a grapheme boundary, and the text has no line break.
- **Owns:** see the module.
- **Access:** dialogs.
- **Referred by:** [text_field (implementation)](./text_field.cpp.skel.md)
- **Referred by:** [file_dialog](./file_dialog.hpp.skel.md)
- **Referred by:** [text_field_test](../../tests/text_field_test.cpp.skel.md)

### function: handle_key

- **Inputs:** `key`: a `KeyEvent`.
- **Returns:** `bool`: true when the key edited or moved inside the field: a printable `Char` (without Ctrl), Backspace, Delete, Left, Right, Home and End. Any other key (Tab, Enter, Escape, Up, Down, PageUp, PageDown, Ctrl combinations) returns false, for the dialog to handle. Left at the start and Right at the end return false too, so the dialog can move focus to the neighbouring zone, as ../modi/ does.
- **State changes:** inserts at the cursor, deletes the grapheme cluster before or after it, or moves the cursor by a cluster or to the text's start or end.
- **Access:** the dialogs.

### function: insert

- **Inputs:** `bytes`: pasted text.
- **Returns:** nothing.
- **State changes:** inserts the text at the cursor with line breaks removed.
- **Access:** dialogs, for a `PasteEvent`.

### function: text

- **Inputs:** none.
- **Returns:** the content as a `const std::string&`; `set_text` replaces it and puts the cursor at the end; `at_start()` and `at_end()` report the cursor's place.
- **State changes:** none for `text`.
- **Access:** dialogs and tests.

### function: render

- **Inputs:** a `Screen&`; `row`, `col`, `width`: the cells the field may fill; `attr`; `focused`.
- **Returns:** nothing.
- **State changes:** draws the visible window of the text from a scroll offset that keeps the cursor inside it, and fills the rest of the width with `attr`. With `focused`, sets the screen cursor at the field's cursor.
- **Access:** dialogs.
