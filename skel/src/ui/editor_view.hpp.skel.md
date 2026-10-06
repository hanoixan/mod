---
role: product
stamp: source b121ff29, stand-in ff9cd780
---
# module: editor_view

Renders the text area and the status line. The layout is:

- Rows 0 … h−2: the text, with an optional line-number gutter. The [menu bar](./menu.hpp.skel.md#class-menubar) is hidden until it is wanted, and then App draws it over row h−2, the last text row (the text does not move).
- Row h−1: the status line, in three blocks. Left: the path (or `[untitled]`) and its flags. Center: notifications, meaning the transient message or a panel's key hints, and when there is none, App's idle hint `Esc h: help`. Right: `line:col` (or `…:col` while unknown), total lines once known, history state, branch indicator, LSP state.

When the [Prompt](./prompt.hpp.skel.md#class-prompt) is open, App puts it below the status lines, on the screen's bottom rows.

The view's position is held as a **byte offset** (`top`), the start of the first visible line, or, with word wrap on, of the first visible **row**, which may lie inside a line. Global line numbers are therefore never needed to scroll; see [PieceTree.line_of](../text/piece_tree.hpp.skel.md#function-line_of).

**Long lines.** With word wrap **off** (the default), each line has one row and the view scrolls sideways to keep the cursor visible. A row whose line continues past the right edge shows a `>` in its last column, in the `overflow_marker` style (reverse video), in place of the character that would be there; a line that ends exactly at the edge has none, and the marker goes when the rest of the line is scrolled into view. Text hidden on the left is not marked. With word wrap **on**, a line continues on the following rows as [WrapLayout](../text/wrap.hpp.skel.md#class-wraplayout) lays it out, at a width of the text columns less one, so the cursor at the end of a full row has a cell; there is no sideways scroll and no marker; the gutter shows a line's number on its first row only; whitespace hanging past the width may show in the spare column, and the cursor never leaves the last column.

- **Owns:** `top`, the horizontal scroll column, the gutter and wrap toggles, and per-frame layout scratch.
- **Access:** public. One instance, owned by App. Main thread.
- **Required:** always.
- **Failure modes:** a very long line on screen. Only bytes up to `hscroll + width` display columns are decoded. Editor's column checkpoints are private to it, so EditorView counts columns itself with [display_width](../text/utf8.hpp.skel.md#function-display_width), and keeps its own for the line last measured (the cursor's): the (offset, column) of a character start every 4 KiB, for the text as it is (its version, size and the tab width), and how far that line is known to have no line feed. The cursor's column, the drawing of that line scrolled far to the right, and its line start all begin from the nearest checkpoint, so moving along a line of any length costs a frame about what a short line does; drawing the other lines never discards the checkpoints. With word wrap the index may be for a row that starts mid-line, so the line start takes the shortcut only when the index starts at a real line start. Other long lines scrolled far right still decode up to the window. The gutter width depends on the line count: while the count is unknown it is estimated from the file size (one line per 16 bytes, at least 3 digits), and once the background scan knows the count, the real count is used. On a large file whose estimate has a different number of digits from the real count, the text therefore shifts once, when the scan completes; never afterwards.
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Depends on:** [Highlighter.spans_for_line](../syntax/highlight.hpp.skel.md#function-spans_for_line)
- **Depends on:** [attr_for](./theme.hpp.skel.md#function-attr_for)
- **Depends on:** [display_width](../text/utf8.hpp.skel.md#function-display_width)
- **Depends on:** [Editor](../edit/editor.hpp.skel.md#class-editor)
- **Depends on:** [WrapLayout](../text/wrap.hpp.skel.md#class-wraplayout)
- **Unknowns:** none. Long lines scroll sideways by default and wrap when View > Word Wrap is on; see [SYSTEM.md](../../SYSTEM.md).

## function: draw_status_line

- **Inputs:** a `Screen&`; `row`; `left`, `message` and `right` texts.
- **Returns:** nothing.
- **State changes:** fills the row in the `status` look and draws the three blocks: `left` from column 0, `right` against the right edge, and `message` centered between them as [render_status](#function-render_status) describes, the right block giving way to a message too long to share the row.
- **Access:** EditorView.render_status, and App for the help viewer's status line.

## class: EditorView

- **Inputs:** `doc`: a `Document&`, non-const only because [PieceTree.line_of](../text/piece_tree.hpp.skel.md#function-line_of) caches the line counts it scans (EditorView never edits); `editor`: a `const Editor&`; `highlighter`: a `Highlighter*`, possibly null, replaced with `set_highlighter`; `tab_width`: 1 to 16.
- **State changes:** invariant: after `scroll_to_cursor`, the cursor is inside the visible text area. It implements [DocumentListener](../edit/document.hpp.skel.md#symbol-documentlistener) to shift `top` when an edit lands before it (and to reset `top` and `hscroll` on `reloaded`); it registers itself with `Document.add_listener` on construction and removes itself on destruction, as Editor does. `top()` and `hscroll()` expose the position.
- **Owns:** see the module.
- **Access:** App.
- **Depends on:** [Document.line_of](../edit/document.hpp.skel.md#function-line_of)
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [editor_view (implementation)](./editor_view.cpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)
- **Referred by:** [document_slot](../app/document_slot.hpp.skel.md)

### function: render

- **Inputs:** a `Screen&`; `area`: a [Rect](./screen.hpp.skel.md#symbol-rect), the text area (gutter included); `search_highlight`: an optional match range; `focused`: whether the cursor belongs to the text (false while a menu, prompt or the history panel has focus).
- **Returns:** nothing.
- **State changes:** draws the text area, every look laid over the theme's page with [on_page](./theme.hpp.skel.md#function-attr) except the overflow marker, and, when `focused`, sets the cursor. Rows after end of file show `~` in the gutter. The status line is drawn separately by `render_status`, so that it is drawn even while the history panel hides the text.
- **Access:** App.render.
- **Depends on:** [PieceTree.find_lf_forward](../text/piece_tree.hpp.skel.md#function-find_lf_forward)
- **Depends on:** [ReadingLayout](./reading_layout.hpp.skel.md#class-readinglayout)

### function: set_preview

- **Inputs:** `marks`: the [preview marks](../edit/document.hpp.skel.md#function-begin_preview) to draw, or nullopt to end the preview mode.
- **Returns:** nothing.
- **State changes:** in preview mode the view draws the document's (previewed) text read-only: no highlighter spans, no selection and no cursor, the inserted marks in `historyInserted` and the removed ones in `historyRemoved`.
- **Access:** App, for the Undo History pane.

### function: set_split_focused

- **Inputs:** `on`: whether this view's split has the focus (true by default).
- **Returns:** nothing.
- **State changes:** the look of the `>` marking a cut line: the `overflowMarker` color while the split has the focus, the theme's `unfocused_status()` otherwise, so a split's markers match its status line. App sets it before drawing each split.
- **Access:** App.

### function: render_status

- **Inputs:** a `Screen&`; `row`: the status row; `message`: the notification for the center (the transient message, a panel's key hints, or App's idle hint), or empty; `mark`: a `StatusMark` (`enum class StatusMark { none, focused, unfocused }`): App passes `focused` for the focused view's status line, with one view or several, and `unfocused` for the others; `focused` starts the line with `>` in place of its leading space, `col` and `width` (default: the whole row) are the columns it is drawn in, App passing the views' left edge beside the folder tree, as [draw_status_line](#function-render_status) takes them too; `unfocused` draws the whole line in [ColorTheme.unfocused_status](./theme.hpp.skel.md#class-colortheme), readable text on a darker band (on a VT100 the same look as the focused one, the `>` then telling the splits apart).
- **Returns:** nothing.
- **State changes:** draws the status line described in the module across the full width: the path or `[untitled]`, `*` when dirty, `line:col` (1-based; the line is `…` while unknown), the total line count once known, the history state when it is neither `attached` nor `session_only` (history kept in memory is the default, so it is not news; left out in a read-only view, which never edits), `[view]` after the name in a read-only view, `branch i/n` when the current node has siblings, the highlighter's `status()`, and `message` centered on the row. The message is centered on the whole row when that clears both side blocks, else placed as near the center as it can between them; when it does not fit between them, the right block is dropped and the message follows the left block, truncated at the edge.
- **Access:** App.render.

### function: set_highlighter

- **Inputs:** a `Highlighter*`, possibly null.
- **Returns:** nothing.
- **State changes:** the next frame asks the new highlighter for spans.
- **Access:** App, when the View toggles or `open_document` swap the highlighter.

### function: scroll_to_cursor

- **Inputs:** `area_rows`, `area_cols`.
- **Returns:** nothing.
- **State changes:** adjusts `top` by walking line feeds and adjusts `hscroll`, with a 3-row (`kRowMargin`) and 8-column margin. With word wrap on, it works in rows instead: `top` is first snapped to a row start (a resize or an edit may have moved the rows), then moved by whole rows with the same 3-row margin, and `hscroll` is 0. When the cursor is out of view (however it got there), the view comes back to it with up to 5 rows (`kReturnMargin`, screen rows) on the side it was beyond: above, 5 rows over it where the document has them; below, as many rows of text as there are after it up to 5, but never fewer than the 3-row margin (or the next follow would move the view again); this margin may reach `rows − 1`, since it is on one side only. Nothing happens while the view is scrolled away (`scroll_rows`) and not yet told to `follow_cursor`.
- **Access:** App, after every command.

### function: scroll_rows

- **Inputs:** `delta`: −1 (Ctrl+Up, the view up a row, so the text and the cursor move down) or +1 (Ctrl+Down); `area_rows`, `area_cols`.
- **Returns:** whether the view moved: false at the first row on top (up) or with the last row on the bottom row (down), so the text always fills the view.
- **State changes:** moves `top` one screen row: a line, a wrapped row with word wrap on, a rendered line in a document laid out for reading. The cursor stays on its text, off screen too. Sets the view as scrolled away, so `scroll_to_cursor` leaves it until `follow_cursor`.
- **Access:** App (`ScrollLineUp`, `ScrollLineDown`), also in the file preview.

### function: follow_cursor

- **Inputs:** none. `scrolled_away()` reads the state back.
- **Returns:** nothing.
- **State changes:** ends the scrolled-away state, so the next `scroll_to_cursor` follows the cursor again (with the return margin when it is out of view).
- **Access:** App, before any command other than the scroll commands, typed text or a paste reaches the document. A resize or other redraw does not, so the view stays where it was scrolled.


`scroll_to(pos, rows, cols, row_margin, margin_above)` is the same for any position: `scroll_to_cursor` is `scroll_to(cursor, …, kRowMargin, false)`. With `margin_above`, a position in the top `row_margin` rows also moves the view, so the margin holds on both sides (the history preview uses 2); without it, as for the cursor, only a position above the view brings it in `row_margin` rows down.
### function: set_tab_width

- **Inputs:** `width`: 1 to 16.
- **Returns:** nothing.
- **State changes:** stores the width passed to `display_width` when laying out lines, and marks the frame for a full redraw. `top` is unchanged; `hscroll` is clamped by the next `scroll_to_cursor`.
- **Access:** App, at startup (the width remembered by [Settings](../app/settings.hpp.skel.md#class-settings), 4 by default) and whenever the `tab_width` setting changes.

### function: set_line_numbers

- **Inputs:** `on`.
- **Returns:** nothing.
- **State changes:** toggles the gutter.
- **Access:** App: from the document's [ViewOptions](../app/document_slot.hpp.skel.md#class-viewoptions) when the view is built, when the `line_numbers` setting changes, and on View > Line Numbers for the focused document. On by default.

### function: set_word_wrap

- **Inputs:** `on`.
- **Returns:** nothing.
- **State changes:** switches between the two layouts described in the module. Turning it on clears `hscroll`; turning it off moves `top` back to the start of its line. `word_wrap()` reads the state.
- **Access:** App: from the document's [ViewOptions](../app/document_slot.hpp.skel.md#class-viewoptions) when the view is built, when the `word_wrap` setting changes, and on View > Word Wrap for the focused document. On by default.

### function: wrap_cols

- **Inputs:** `area_cols`: the width of the text area, gutter included.
- **Returns:** the columns a wrapped row may fill there: the text columns (the area less the gutter) less one, and at least 1.
- **State changes:** none.
- **Access:** `render` and `scroll_to_cursor`, and App, which passes the value to [Editor.set_wrap_width](../edit/editor.hpp.skel.md#function-set_wrap_width) so that the cursor moves through the rows that are drawn.

### function: set_top

- **Inputs:** `offset`: a byte offset.
- **Returns:** nothing.
- **State changes:** makes the line containing `offset` the first one shown (with word wrap, the row containing it, found at the next scroll or frame). The next `scroll_to_cursor` still brings the cursor into view.
- **Access:** App, when read-only mode shows a place from its trail.

### function: set_read_only

- **Inputs:** `on`.
- **Returns:** nothing.
- **State changes:** marks the status line with `[view]` and leaves the history state off it.
- **Access:** App, when read-only mode changes and when a document is shown in it.

### function: set_reading

- **Inputs:** `on`: whether to draw the document laid out for reading (App sets it for a Markdown document in read-only mode).
- **Returns:** nothing.
- **State changes:** while on, and the text area is at least 21 columns wide (gutter aside), the view draws a [ReadingLayout](./reading_layout.hpp.skel.md#class-readinglayout) of the document instead of its lines: made again when the document's version or the width changes, always wrapped (Word Wrap does not apply), scrolled by rendered line, and **panned sideways** as a whole when the cursor's column is past the right edge (a wide table or code line, which the layout does not wrap): the view keeps the cursor on screen with the same margin as unwrapped editing, and comes back to column 0 when the cursor's column fits from there; a line running past the right edge shows `>` in the `overflow_marker` style in the last column, the gutter numbering each source line on the first rendered line that comes from it, the selection and the search match drawn over the bytes whose source is in them, the cursor at [locate](./reading_layout.hpp.skel.md#function-locate) of the editor's cursor. Styles come from the layout's spans when the view has a highlighter (Syntax Coloring is on), and none otherwise. A narrower area draws the lines as usual.
- **Access:** App.

### function: reading_layout

- **Inputs:** none.
- **Returns:** the layout drawn by the last frame, or nullptr when the view is not drawing one; it is valid until the next frame or change.
- **State changes:** none.
- **Access:** App: motions and copy.
