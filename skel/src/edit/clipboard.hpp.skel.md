---
role: product
stamp: source 53b47047, stand-in 29648d42
---
# module: clipboard

The copy and paste store. The internal clipboard always works and holds a `PieceRun`, so copying a multi-GB selection costs O(pieces). In addition, every copy of at most `kOsc52MaxBytes` = 100 KB (100 000 bytes of selected text, measured before base64 encoding) is also sent to the terminal's clipboard through the OSC 52 escape sequence, which most modern terminals honor for *writing*. This is always on; there is no setting or menu toggle. Larger copies stay internal only. External clipboard tools (`wl-copy`, `xclip`, `pbcopy`, `clip.exe`) are never used. Reading the system clipboard is not attempted: terminal paste (Ctrl+Shift+V or Cmd+V) arrives as bracketed paste input instead.

- **Owns:** the current clipboard content.
- **Access:** public. One per process, owned by [App](../app/app.hpp.skel.md#class-app), so it survives File > Open. Main thread.
- **Required:** always.
- **Failure modes:** the clipboard holds a `PieceRun` into a document that is closed afterwards. The run keeps the buffers alive through the shared_ptrs in the buffer table, which App passes along when it replaces the document. Some terminals ignore OSC 52, and inside tmux it needs `set-clipboard on`. Both fail silently: `mod` cannot tell whether OSC 52 reached the system clipboard. A copy over 100 KB never reaches other applications; that is accepted. The base64 form of a 100 000-byte copy is about 133 KB, which some terminals still truncate or drop; also accepted.
- **Depends on:** [PieceRun](../text/piece_tree.hpp.skel.md#symbol-piecerun)

- **Unknowns:** none

## class: Clipboard

- **Inputs:** `terminal_write`: a sink used for OSC 52, a `std::function<void(std::string_view)>`; it may be empty (tests, or no terminal).
- **State changes:** holds the last copied content and its source buffer keep-alive.
- **Owns:** the content.
- **Access:** Editor and App.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [clipboard (implementation)](./clipboard.cpp.skel.md)
- **Referred by:** [editor](./editor.hpp.skel.md)
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)

### function: set

- **Inputs:** `run`: a `PieceRun`; `buffers`: keep-alive handles, one [FrozenBytes](../text/piece_tree.hpp.skel.md#symbol-frozenbytes) view per piece of `run`, which also let another document read the bytes; `source`: the [Document.id](./document.hpp.skel.md#function-id) the run belongs to; `materialize`: a callback producing bytes, called only when the run's length is at most `kOsc52MaxBytes`.
- **Returns:** nothing.
- **State changes:** replaces the content. If the run's length is at most `kOsc52MaxBytes`, it materializes the bytes and writes `ESC]52;c;<base64>ESC\` through `terminal_write`; otherwise it writes nothing to the terminal. Cut uses the same path as copy.
- **Access:** Editor.copy and Editor.cut.

### function: set_text

- **Inputs:** `text`: bytes that belong to no document (`source` 0, which no document id is).
- **Returns:** nothing.
- **State changes:** replaces the content with `text`, held alive by its one view, and writes OSC 52 the way `set` does. A paste copies the bytes, as from another document.
- **Access:** App: copying the visible text of read-only Markdown.

### function: append

- **Inputs:** `run`, `views`, `source`, as for `set`.
- **Returns:** nothing.
- **State changes:** when the clipboard holds content from the same document (`source`), joins `run` and `views` onto its end, so the content is the old bytes followed by the new; otherwise replaces the content, as `set` does. Either way OSC 52 carries the whole resulting content when it is at most `kOsc52MaxBytes`, read back through the views.
- **Access:** Editor.cut_to_line_end.

### function: get

- **Inputs:** none.
- **Returns:** a pointer to the `ClipContent { run; views; source; length; }`, or `nullptr` when the clipboard is empty.
- **State changes:** none.
- **Access:** Editor.paste. When `source` is not the pasting document's id, the run's buffers are not in that document's table, so Editor pastes a copy of the bytes, read through `views` (see the implementation).

### function: rebind

- **Inputs:** `run`, `views`, `source`: an equivalent replacement for the current content (same bytes, same length).
- **Returns:** nothing.
- **State changes:** replaces the run and its keep-alive handles. Nothing is written to the terminal.
- **Access:** [Document.save](./document.hpp.skel.md#function-save) with `in_place`, after copying the content into the add buffer.
- **Referred by:** [Document.save](./document.hpp.skel.md#function-save)
