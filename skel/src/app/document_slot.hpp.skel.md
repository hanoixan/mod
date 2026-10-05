---
role: product
untested: a plain holder of components; it is exercised through App, which no automated test drives
stamp: source de67e234, stand-in 8f92efe6
---
# module: document_slot

One view of a document with everything built on it, and the document's View options: its [Editor](../edit/editor.hpp.skel.md#class-editor), [Searcher](../search/search.hpp.skel.md#class-searcher), [EditorView](../ui/editor_view.hpp.skel.md#class-editorview) and [Highlighter](../syntax/highlight.hpp.skel.md#class-highlighter), and the highlighter's next deadline. App holds the slot on screen as `shown_`. Showing another document means moving the shown slot aside and moving another one in, so that a document set aside keeps its cursor, scroll, search and coloring exactly as they were. Read-only mode does this when it shows a link target.

- **Owns:** the five components and their lifetimes.
- **Access:** public. Owned by App. Main thread.
- **Required:** always.
- **Failure modes:** destroying the parts in the wrong order (a view or highlighter outliving its document) would leave listeners pointing at freed memory; `reset` and the destructor tear down in dependency order.
- **Depends on:** [Document](../edit/document.hpp.skel.md#class-document)
- **Depends on:** [Editor](../edit/editor.hpp.skel.md#class-editor)
- **Depends on:** [Searcher](../search/search.hpp.skel.md#class-searcher)
- **Depends on:** [EditorView](../ui/editor_view.hpp.skel.md#class-editorview)
- **Depends on:** [Highlighter](../syntax/highlight.hpp.skel.md#class-highlighter)
- **Unknowns:** none

## class: ViewOptions

A document's View options, shared (`std::shared_ptr`) by every slot that views the document, so its split views always agree: `line_numbers`, `syntax`, `word_wrap` and `read_only`. A new document's come from the `line_numbers`, `syntax_coloring` and `word_wrap` settings, with `read_only` off.

- **Inputs:** none; a plain aggregate.
- **State changes:** App sets the fields; the slots' views follow when App applies them.
- **Owns:** nothing.
- **Access:** App.

## class: DocumentSlot

- **Inputs:** none; App fills the members. Movable, not copyable; move-assigning first resets the target. The **document and its highlighter are shared** (`std::shared_ptr`) between every slot that shows the document, one per split view, and so are its `options` ([ViewOptions](#class-viewoptions)); the editor, searcher and view are the slot's own, so each split has its own cursor, selection, search and scroll.
- **State changes:** either empty, or all of `doc`, `editor`, `searcher` and `view` set (the highlighter is optional). A highlighter, when present, is registered as a listener of `doc` and set on `view`.
- **Owns:** see the module.
- **Access:** App.
- **Referred by:** [app](./app.hpp.skel.md)
- **Referred by:** [document_slot (implementation)](./document_slot.cpp.skel.md)
- **Referred by:** [workspace](./workspace.hpp.skel.md)
- **Referred by:** [history_preview](./history_preview.hpp.skel.md)

### function: reset

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** removes the highlighter from the view and drops this slot's share of it (the last share removes it from the document's listeners), then destroys the view, the searcher and the editor and drops this slot's share of the document, in that order. The destructor does the same.
- **Access:** App, and the move assignment.
