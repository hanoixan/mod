---
role: product
stamp: source 4333aad0, stand-in f537a908
---
# module: layered_highlighter

Two highlighters as one: a **base** (the syntax layer) and an **overlay** (a language server's semantic tokens). The overlay's spans are drawn over the base's: where they overlap, the overlay wins, and the base's span is cut around it. So keywords, strings and comments come from the syntax layer and names from the server, and when the server is off, starting or has nothing for a line, the syntax layer alone shows.

- **Owns:** both highlighters.
- **Access:** public. Created by App when a file gets both layers. Main thread.
- **Required:** optional.
- **Failure modes:** none of its own; each layer's failures stay that layer's.
- **Depends on:** [Highlighter](./highlight.hpp.skel.md#class-highlighter)
- **Unknowns:** none

## class: LayeredHighlighter

- **Inputs:** `base` and `overlay`: `std::unique_ptr<Highlighter>`, both for the same document.
- **State changes:** none of its own. Every `DocumentListener` event is forwarded to the base, then the overlay, so App registers only the LayeredHighlighter.
- **Owns:** see the module.
- **Access:** App, through `Highlighter`.
- **Referred by:** [syntax_highlighter_test](../../tests/syntax_highlighter_test.cpp.skel.md)
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [layered_highlighter (implementation)](./layered_highlighter.cpp.skel.md)

### function: spans_for_line

- **Inputs:** as [Highlighter.spans_for_line](./highlight.hpp.skel.md#function-spans_for_line).
- **Returns:** the base's spans with every overlay span laid over them, sorted and not overlapping.
- **State changes:** whatever the two layers' own calls change.
- **Access:** EditorView.

### function: tick

- **Inputs:** `now`.
- **Returns:** the earlier of the two layers' deadlines. `visible_range_changed` is forwarded to both, and `status()` is the overlay's.
- **State changes:** runs both layers' timers.
- **Access:** App.
