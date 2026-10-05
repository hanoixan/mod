---
role: product
stamp: source 6ea0da02, stand-in c4f25e66
---
# module: editor

Cursor, selection and editing commands over a [Document](./document.hpp.skel.md#class-document). It translates user intents (move by word, delete the selection, paste) into `Document.apply` calls and cursor updates. It knows nothing about rendering, except that the page size and the preferred display column are given to it.

- **Owns:** the cursor offset, the selection anchor, the preferred display column (the "sticky column" for Up/Down), and the reference to the clipboard.
- **Access:** public. One per Document, owned by [App](../app/app.hpp.skel.md#class-app). Main thread.
- **Required:** always.
- **Failure modes:** the cursor lands inside a UTF-8 sequence or between CR and LF. After every motion and edit, the cursor is snapped to a code-point boundary and never left between `\r` and `\n`. Cluster boundaries are not an invariant of the cursor, only of the motions: a search match, a position restored by undo, or an edit next to the cursor (typing a combining mark after it) can leave the cursor inside a cluster, and the next Left, Right, Backspace or Delete then works from there.
- **Depends on:** [Document](./document.hpp.skel.md#class-document)
- **Depends on:** [Clipboard](./clipboard.hpp.skel.md#class-clipboard)
- **Depends on:** [utf8](../text/utf8.hpp.skel.md)
- **Unknowns:** none

## symbol: Motion

`enum class Motion`:

- `Left`, `Right`: one extended grapheme cluster, from [prev_grapheme_boundary](../text/utf8.hpp.skel.md#function-prev_grapheme_boundary) and [next_grapheme_boundary](../text/utf8.hpp.skel.md#function-next_grapheme_boundary).
- `WordLeft`, `WordRight`: Ctrl+Left/Right.
- `Up`, `Down`: by display column.
- `LineStart`: Home or Ctrl+A.
- `LineEnd`: End or Ctrl+E.
- `PageUp`, `PageDown`.
- `DocStart`: Ctrl+Home.
- `DocEnd`: Ctrl+End.

- **Access:** public.
- **Referred by:** [reading_layout](../ui/reading_layout.hpp.skel.md)

## class: Editor

- **Inputs:** `doc`: a `Document&`; `clipboard`: a `Clipboard&`; `tab_width`: the current tab width, for display columns. The Editor registers itself as a [DocumentListener](./document.hpp.skel.md#symbol-documentlistener) for its lifetime: an edit made elsewhere shifts the cursor and anchor (positions after the edit move by its length delta, positions inside the removed range move to its start), and `reloaded` clamps the cursor and clears the selection.
- **State changes:** invariants: `0 ≤ cursor ≤ doc.size()`. `anchor` is set if and only if a selection exists. A selection is never empty: if `anchor == cursor`, the anchor is cleared. After every change of the document (its own or another view's), the cursor and anchor are shifted and then snapped to a character boundary: an edit can join invalid bytes on either side into one valid character, which they must not end up inside.
- **Owns:** see the module.
- **Access:** App command dispatch.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [editor (implementation)](./editor.cpp.skel.md)
- **Referred by:** [editor_view](../ui/editor_view.hpp.skel.md)
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [document_slot](../app/document_slot.hpp.skel.md)
- **Referred by:** [fuzz_editor](../../fuzz/fuzz_editor.cpp.skel.md)

### function: move

- **Inputs:** `motion`; `extend`: true when Shift is held, which sets the anchor if needed; `page_rows`: for PageUp and PageDown.
- **Returns:** nothing.
- **State changes:** updates the cursor and anchor. `Up`, `Down`, `PageUp` and `PageDown` keep the sticky column; every other motion resets it. Those four move by line, or by screen row while a wrap width is set ([set_wrap_width](#function-set_wrap_width)). A non-extending motion with a selection collapses it: Left goes to the selection start and Right to its end. Word motion follows the VS Code rule: skip whitespace, then skip a run of the same [CharClass](../text/utf8.hpp.skel.md#symbol-charclass). It steps by whole grapheme clusters, classifying each cluster by its first code point, so it never stops inside one. Line feeds are their own stop.
- **Access:** App.
- **Depends on:** [PieceTree.find_lf_backward](../text/piece_tree.hpp.skel.md#function-find_lf_backward)
- **Depends on:** [next_grapheme_boundary](../text/utf8.hpp.skel.md#function-next_grapheme_boundary)
- **Depends on:** [prev_grapheme_boundary](../text/utf8.hpp.skel.md#function-prev_grapheme_boundary)

### function: insert_text

- **Inputs:** `bytes`, from typing or from [paste_text](#function-paste_text), or a single `\t` for the Tab key (`InsertTab`); `kind`: `typing` or `paste`. A tab is typed text: it coalesces with neighboring typing like any other character.
- **Returns:** nothing.
- **State changes:** replaces the selection, if any, within one group, then inserts. The cursor moves after the inserted text.
- **Access:** App.

### function: indent

- **Inputs:** `spaces`: whether to indent with spaces.
- **Returns:** nothing.
- **State changes:** with `spaces`, inserts spaces from the cursor (or the start of a selection, which is replaced) up to the next multiple of the tab width in display columns; otherwise inserts one tab character. Typing for the undo history.
- **Access:** App, for `InsertTab`.

### function: outdent

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** from every line the selection touches (the cursor's line without a selection; a selection ending at a line's start does not touch that line), removes one leading tab, or else up to a tab width of leading spaces, all as one undo step; the cursor and the selection's anchor move left with their text.
- **Access:** App, for `Outdent`.

### function: set_tab_width

- **Inputs:** `width`: 1 to 16.
- **Returns:** nothing.
- **State changes:** stores the width used to compute display columns for Up, Down and the sticky column. The cursor's byte offset does not move; the sticky column is reset, because columns measured with the old width are meaningless.
- **Access:** App, whenever the `tab_width` setting changes.

### function: set_wrap_width

- **Inputs:** `columns`: the wrap width the view is drawing with, or `nullopt` when word wrap is off.
- **Returns:** nothing.
- **State changes:** with a width, `Up`, `Down`, `PageUp` and `PageDown` move by **screen row** of the [WrapLayout](../text/wrap.hpp.skel.md#class-wraplayout) for that width: Down from the first row of a wrapped line goes to its second row, not to the next line. The sticky column is then the display column inside the row, with tab stops counted from the row's start, as the view draws it. A target on a row that is not its line's last never lands on that row's end, because that offset is the start of the next row. `LineStart` and `LineEnd` stay whole-line. With `nullopt` the four motions move by line again. A change of width resets the sticky column.
- **Access:** App, before every command and after every layout change, with [EditorView.wrap_cols](../ui/editor_view.hpp.skel.md#function-wrap_cols).
- **Depends on:** [WrapLayout](../text/wrap.hpp.skel.md#class-wraplayout)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)

### function: newline

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** inserts the document's line ending. No auto-indent, which is not requested.
- **Access:** App (Enter).

### function: delete_backward

- **Inputs:** `word`: bool, for Ctrl+Backspace if the terminal sends it.
- **Returns:** nothing.
- **State changes:** deletes the selection, or the previous extended grapheme cluster: a whole emoji ZWJ sequence, a whole flag, a base character with all its combining marks, or a CR LF pair (one cluster under UAX #29 rule GB3, which keeps the existing CRLF-as-one-unit rule). With `word`, deletes back to the previous word stop as `WordLeft` finds it.
- **Access:** App.

### function: delete_forward

- **Inputs:** `word`: bool.
- **Returns:** nothing.
- **State changes:** as `delete_backward`, but forwards: deletes the next extended grapheme cluster, or up to the next word stop with `word`.
- **Access:** App (Delete).

### function: copy

- **Inputs:** none.
- **Returns:** whether anything was copied: false when there is no selection.
- **State changes:** puts the selection into the clipboard as a `PieceRun`, with no byte copy. With no selection it does nothing: the clipboard keeps its content, nothing is sent to the terminal, and no status message is shown. (It does not copy the current line as VS Code does.)
- **Access:** App (Ctrl+C, Edit > Copy).

### function: cut

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** copies, then deletes the selection as one `cut` node. With no selection it does nothing, like `copy`: no node is created and the clipboard is unchanged.
- **Access:** App (Ctrl+X, Edit > Cut).

### function: cut_to_line_end

- **Inputs:** `append`: true when the previous input event was also this command (App tracks it), so the cut joins the clipboard instead of replacing it.
- **Returns:** nothing.
- **State changes:** with a selection, cuts the selection. Otherwise cuts from the cursor to the end of its line (before a CR LF or LF); when the cursor is already at the end of the line, cuts the line break instead (a CR LF whole), which joins the next line onto this one; at the end of the text it does nothing, leaving the clipboard and history unchanged. Each press is one `cut` node. With `append` the cut text goes through [Clipboard.append](./clipboard.hpp.skel.md#function-append), so pressing it N times and then pasting gives back all N pieces in order. The cursor stays at the start of the cut.
- **Access:** App (Ctrl+K, Edit > Cut to Line End).
- **Depends on:** [Clipboard.append](./clipboard.hpp.skel.md#function-append)

### function: paste

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** inserts the clipboard content with [Document.apply](./document.hpp.skel.md#function-apply) using a `PieceRun` (no copy), replacing the selection, as one `paste` node. Content copied from another document (its `source` differs from [Document.id](./document.hpp.skel.md#function-id)) is pasted as a copy of its bytes, read through the clipboard's views, because its buffers are not in this document's table. An empty clipboard does nothing.
- **Access:** App (Ctrl+V, Edit > Paste).

### function: paste_text

- **Inputs:** `bytes`: the text of a bracketed paste, or of one piece of it, exactly as the terminal sent it; `more`: another piece of the same paste follows (default false).
- **Returns:** nothing.
- **State changes:** inserts the text as one `paste` node (the pieces of one paste go into one node, through an explicit [Document](./document.hpp.skel.md#class-document) group opened at the first piece and closed at the last, or by the Editor's destructor if the last never comes; two pastes are two nodes), replacing the selection, with every line break turned into the document's line ending (the one `newline` inserts). A line break is CR LF, a lone CR or a lone LF, each counted once. Terminals send each newline of a paste as a bare CR, which would otherwise be stored as a control character and shown as `^M`. Nothing else in the text is changed. The cursor moves after the inserted text.
- **Access:** App (a `PasteEvent`). The internal clipboard does not come this way; see `paste`.
- **Depends on:** [Document.line_ending](./document.hpp.skel.md#function-line_ending)

### function: cursor

- **Inputs:** none.
- **Returns:** `cursor()`: the offset; `anchor()`: the optional anchor; `selection()`: the optional `(start, end)` with `start < end`.
- **State changes:** none.
- **Access:** EditorView, Searcher and App.

### function: select_range

- **Inputs:** `start`, `end`.
- **Returns:** nothing.
- **State changes:** sets the anchor to `start` and the cursor to `end`.
- **Access:** Searcher, to highlight a match.

### function: undo

- **Inputs:** none.
- **Returns:** `Status`: the error from `Document.undo` (`canceled` at a root, `unsupported` while verifying), for the status line; the cursor is unchanged on error.
- **State changes:** calls `Document.undo`, sets the cursor to the result, and clears the selection.
- **Access:** App (Ctrl+Z).

### function: redo

- **Inputs:** none.
- **Returns:** `Status`, as `undo`.
- **State changes:** as `undo`.
- **Access:** App (Ctrl+Y).
