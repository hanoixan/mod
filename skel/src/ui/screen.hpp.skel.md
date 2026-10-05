---
role: product
stamp: source 1f7e27b8, stand-in adfd1755
---
# module: screen

A double-buffered cell grid that renders to VT escape sequences. Each frame is drawn into the back buffer, and `flush` emits only the cells that changed, in one `Terminal.write`, wrapped in synchronized-output markers (`ESC[?2026h` … `ESC[?2026l`) that terminals without support ignore.

- **Owns:** the front and back cell buffers and the output byte buffer.
- **Access:** public. One instance, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** always.
- **Failure modes:**
  - A wide character in the last column is replaced by a space, so the terminal never auto-wraps.
  - The front buffer disagrees with the terminal after a resize or an external write: `invalidate` forces a full repaint, followed by `ESC[2J`.
  - Some terminals render italic (SGR 3) as reverse video or ignore it, which is accepted.
  - Writing the bottom-right cell can scroll some terminals, so it is never written last with auto-wrap on. Use `ESC[?7l` to disable auto-wrap during rendering.
- **Depends on:** [Terminal.write](../platform/terminal.hpp.skel.md#function-write)
- **Depends on:** [TerminalOutput](../platform/terminal_output.hpp.skel.md#class-terminaloutput)
- **Unknowns:** none

## symbol: Attr

`{ uint8_t fg; uint8_t bg; uint8_t flags; }`, comparable with `==`. The constants `kDefaultColor` (255) and the flag bits `kBold`, `kDim`, `kItalic`, `kUnderline`, `kReverse` and `kStrike` are public.

- `fg` and `bg` are 0–15 for the ANSI colors, or 255 for the terminal default.
- `flags` is a bit set: bold, dim, italic, underline, reverse, strike.
- Only the 16-color palette is used; see [SYSTEM.md](../../SYSTEM.md).

- **Access:** public.
- **Referred by:** [theme](./theme.hpp.skel.md)
- **Referred by:** [terminal_output](../platform/terminal_output.hpp.skel.md)

## symbol: Rect

`{ int row; int col; int rows; int cols; }`: a rectangle of cells. App computes the areas (text area, history pane, prompt rows) and hands them to the views, which never draw outside them.

- **Access:** public.

## symbol: Cell

`{ std::array<char, 8> utf8; uint8_t len; uint8_t width; Attr attr; }`. A width-2 cell is followed by a continuation cell with `width = 0`. Sequences longer than 8 bytes, such as long combining runs, are truncated to the base character.

- **Access:** public.

## class: Screen

- **Inputs:** `terminal`: a `Terminal&`.
- **State changes:** invariant: the front buffer equals what the terminal currently shows.
- **Owns:** see the module.
- **Access:** views draw into it during `App.render`.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [editor_view](./editor_view.hpp.skel.md)
- **Referred by:** [menu](./menu.hpp.skel.md)
- **Referred by:** [prompt](./prompt.hpp.skel.md)
- **Referred by:** [screen (implementation)](./screen.cpp.skel.md)
- **Referred by:** [history_view](./history_view.hpp.skel.md)
- **Referred by:** [keymap_view](./keymap_view.hpp.skel.md)
- **Referred by:** [settings_view](./settings_view.hpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)
- **Referred by:** [keymap_view_test](../../tests/keymap_view_test.cpp.skel.md)
- **Referred by:** [settings_view_test](../../tests/settings_view_test.cpp.skel.md)
- **Referred by:** [menu_test](../../tests/menu_test.cpp.skel.md)
- **Referred by:** [doc_search_view](./doc_search_view.hpp.skel.md)
- **Referred by:** [doc_search_test](../../tests/doc_search_test.cpp.skel.md)
- **Referred by:** [colors_view](./colors_view.hpp.skel.md)
- **Referred by:** [colors_view_test](../../tests/colors_view_test.cpp.skel.md)
- **Referred by:** [terminal_output_test](../../tests/terminal_output_test.cpp.skel.md)
- **Referred by:** [help_viewer](./help_viewer.hpp.skel.md)
- **Referred by:** [help_viewer_test](../../tests/help_viewer_test.cpp.skel.md)
- **Referred by:** [confirm_bar](./confirm_bar.hpp.skel.md)
- **Referred by:** [confirm_bar_test](../../tests/confirm_bar_test.cpp.skel.md)
- **Referred by:** [file_dialog](./file_dialog.hpp.skel.md)
- **Referred by:** [text_field](./text_field.hpp.skel.md)
- **Referred by:** [file_dialog_test](../../tests/file_dialog_test.cpp.skel.md)
- **Referred by:** [text_field_test](../../tests/text_field_test.cpp.skel.md)

### function: resize

- **Inputs:** a `TerminalSize`.
- **Returns:** nothing.
- **State changes:** reallocates both buffers and invalidates.
- **Access:** App, on resize.

### function: cursor_row

- **Inputs:** none.
- **Returns:** `cursor_row()`, `cursor_col()` and `cursor_visible()`: where `set_cursor` last put the cursor, which is where it will be after the next `flush`.
- **State changes:** none.
- **Access:** tests.

### function: put

- **Inputs:** `row`, `col`, `text`: one display cell's UTF-8; `width`: 1 or 2, or 0 for a zero-width code point, which is appended to the cell before `col` (up to the 8-byte limit); `attr`.
- **Returns:** the number of columns consumed. Writes outside the bounds are clipped.
- **State changes:** writes to the back buffer. **Nothing drawn can reach the terminal as a control:** text holding a C0 control, DEL, a C1 control (U+0080–U+009F), or any byte that is not UTF-8 (a lone 0x80–0x9F byte is a C1 control to a terminal reading 8-bit controls) is drawn as `?` one column wide, and a width outside 0–2 is clamped, so a file name, a message or a pasted character can never send an escape sequence or throw the grid off.
- **Access:** views.

### function: print

- **Inputs:** `row`, `col`, `col_end`, `text`: UTF-8, `attr`.
- **Returns:** the column after the last cell drawn.
- **State changes:** draws `text` cell by cell with [display_width](../text/utf8.hpp.skel.md#function-display_width) (tab width 1), up to but not including `col_end`. A wide character that would cross `col_end` stops the drawing; control characters show as a space and invalid bytes as `?`. For labels, prompts and the status line, never for document text, which EditorView lays out itself.
- **Access:** views.

### function: cell

- **Inputs:** `row`, `col`.
- **Returns:** the back buffer's `Cell` there; `rows()` and `cols()` give the size.
- **State changes:** none.
- **Access:** views (for the size) and tests.

### function: add_flags

- **Inputs:** `area`; `flags` (such as `kDim`).
- **Returns:** nothing.
- **State changes:** adds `flags` to the attributes of the cells of `area` already drawn in the back buffer, clipped to the screen; their text and colors stay.
- **Access:** App (the other splits' text dims while the bottom band is open).

### function: fill

- **Inputs:** `row`, `col_from`, `col_to`, `attr`.
- **Returns:** nothing.
- **State changes:** fills the range with spaces.
- **Access:** views.

### function: set_cursor

- **Inputs:** `row`, `col`, `visible`.
- **Returns:** nothing.
- **State changes:** sets the cursor position applied at `flush`.
- **Access:** EditorView or Prompt, whichever has focus.

### function: flush

- **Inputs:** none.
- **Returns:** `Status`.
- **State changes:** diffs back against front, builds the frame through its [TerminalOutput](../platform/terminal_output.hpp.skel.md#class-terminaloutput) (the frame's prefix and suffix, cursor moves `ESC[r;cH`, the output's SGR for each change of look, text), writes it, and copies back to front. `set_output` chooses the output (xterm until then) and invalidates the screen. `set_cursor_style` chooses the cursor's shape (a bar until then); the frame sends it through [cursor_shape](../platform/terminal_output.hpp.skel.md#function-cursor_shape) when it differs from the last one sent, and after every full redraw.
- **Access:** App, once per loop iteration when anything changed, and from App's progress sink while a long operation blocks the loop.

### function: invalidate

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** marks the front buffer unknown, forcing a full repaint.
- **Access:** App, on resize and after OSC 52 writes.
