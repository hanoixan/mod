---
role: test
stamp: source e728ba98, stand-in 4c147d22
---
# module: editor_view_test

Renders an [EditorView](../src/ui/editor_view.hpp.skel.md#class-editorview) over a real Document to a `Screen` with a null terminal. Wrap off: a line that runs past the right edge ends in `>` in the `overflow_marker` style (reverse video) in the last column, a line exactly as wide as the area has none, nor has a short line; the marker goes once the rest of the line is scrolled into view; it sits at the right edge with a gutter, and a wide character cut by the edge counts as overflow. Wrap on: a long line continues on the next rows, broken at words, with no marker and no sideways scroll; the wrap width is one less than the text columns; the gutter numbers a line's first row only; the cursor is drawn on its row, Down moves one screen row, the line end stays on the line's last row, and an unfocused view leaves the cursor alone; a selection is shown on every row it covers; scrolling works by rows inside one very long line, with `top` on a row start inside the line; turning wrap off brings back one row per line and sideways scrolling, and turning it on clears the sideways scroll; an edit above the view keeps the same text at the top. `set_top` shows the line containing an offset first; the status line marks a read-only view `[view]` and leaves its history state off. The status line centers its message between the name and the position, and drops the position block when the message does not fit between them. In paper the text and the empty area are drawn on the page, and the overflow marker is not. History kept in memory shows no history state on the status line. In preview mode the inserted mark is drawn in `historyInserted`, the removed one in `historyRemoved`, with no selection. Reading mode: the layout drawn with marks hidden and the cursor at its source byte, the selection over shown bytes, the gutter numbering each source line once, a narrow area drawing lines, and scrolling by rendered line. Reading mode pans sideways to keep the cursor at a wide line's end visible, pans back at the line start, and marks a line past the edge with `>`. A text area one to three columns wide draws with line numbers in every mode without throwing. Moving along a 4 MB line costs a frame no more than a short line does (timed, with [time_budget](./time_budget.hpp.skel.md#function-time_budget)). With word wrap the status line counts the column from the line's start, not the row's. With several splits the focused status line starts with '>' and the others are on the theme's darker band; with one view, neither. scroll_to with the margin on both sides keeps a place on screen and out of the margin rows, moving the least it can, with and without wrap. A cut line's overflow marker takes its split's status look, focused or unfocused; the marker looks like the focused status line, not like text.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** line numbers come from the background scan, so the fixture drains the event queue until the line count is known before drawing; no timing is involved.
- **Depends on:** [EditorView](../src/ui/editor_view.hpp.skel.md#class-editorview)
- **Depends on:** [Editor.set_wrap_width](../src/edit/editor.hpp.skel.md#function-set_wrap_width)
- **Depends on:** [WrapLayout](../src/text/wrap.hpp.skel.md#class-wraplayout)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [Clipboard](../src/edit/clipboard.hpp.skel.md#class-clipboard)
- **Depends on:** [sidecar_path_for](../src/edit/sidecar.hpp.skel.md#function-sidecar_path_for)
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [attr_for](../src/ui/theme.hpp.skel.md#function-attr_for)
- **Depends on:** [EventQueue.drain](../src/util/event_queue.hpp.skel.md#function-drain)
- **Depends on:** [EditorView.set_read_only](../src/ui/editor_view.hpp.skel.md#function-set_read_only)
- **Depends on:** [set_reading](../src/ui/editor_view.hpp.skel.md#function-set_reading)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
