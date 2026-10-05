---
role: product
stamp: source 2e7d0184, stand-in 73592e06
---
# module: history_preview

The Undo History pane's preview, kept apart from App so it can be tested without a terminal. While the [HistoryView](../ui/history_view.hpp.skel.md#class-historyview) is open, the focused view shows the selected node's text read-only, at the lines it showed when the pane opened, and puts everything back when the pane closes.

- **Owns:** the remembered place (the view's top offset and its line) and which side, the pane or the text, has the focus.
- **Access:** App. Main thread.
- **Required:** always.
- **Failure modes:** a node whose text cannot be rebuilt: [show](#function-show) returns the error (App shows "cannot preview: <reason>") and the view shows the text without its marks.
- **Depends on:** [DocumentSlot](./document_slot.hpp.skel.md#class-documentslot)
- **Depends on:** [Document.begin_preview](../edit/document.hpp.skel.md#function-begin_preview)
- **Unknowns:** none

## class: HistoryPreview

- **Inputs:** none at construction; each call takes the slot it works on (the focused view's).
- **State changes:** `begin` remembers the place and sets the focus to the pane; `end` restores and sets it to the pane again; `toggle_focus` (Tab and Shift+Tab in the pane) switches it.
- **Owns:** the remembered place and the focus flag.
- **Access:** App.
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [history_preview (implementation)](./history_preview.cpp.skel.md)
- **Referred by:** [history_preview_test](../../tests/history_preview_test.cpp.skel.md)

### function: begin

- **Inputs:** `slot`; `node`: the pane's selected node, or nullopt; `rows`, `cols`: the text area.
- **Returns:** as [show](#function-show).
- **State changes:** remembers the view's top offset and the line it is on, takes the highlighter off the view (its cached state would not match the previewed text), gives the pane the focus, then shows `node`.
- **Access:** App, when the pane opens or after a pane command that may change the history.

### function: show

- **Inputs:** `slot`; `node`: the node to show, or nullopt for a row without one (the current state is shown); `rows`, `cols`: the text area.
- **Returns:** success, or the error of `Document.begin_preview`.
- **State changes:** the document previews the node and the view shows its marks (none on an error). A step with a change: from what is on screen, the view scrolls the least that puts the change (the last mark's start) on screen `kChangeMargin` (2) rows clear of the top and the bottom ([EditorView.scroll_to](../ui/editor_view.hpp.skel.md#function-scroll_to_cursor) with the margin on both sides). A step without one: the view's top is the start of the remembered line in that text (else the remembered offset, clamped).
- **Access:** App, when the pane's selection changes.

### function: end

- **Inputs:** `slot`.
- **Returns:** nothing.
- **State changes:** ends the document's preview, clears the view's, puts the view's top back at the remembered offset (clamped) and its highlighter back; gives the pane the focus. Safe to call when nothing is previewing.
- **Access:** App, when the pane closes, jumps, or runs a command.

### function: scroll

- **Inputs:** `slot`; `key`; `page_rows`: the lines of a page (at least 1 is used).
- **Returns:** whether `key` was a motion: Up and Down move the top one line, Page Up and Page Down a page, Home to the start, End to the last page.
- **State changes:** the view's top.
- **Access:** App, while the text has the focus.
