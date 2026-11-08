---
role: product
stamp: source 0dd47a93, stand-in 4cf6341b
---
# module: layered_highlighter

Two highlighters as one: a **base** (the syntax layer) and an **overlay** (a language server's semantic tokens). The overlay's spans are drawn over the base's: where they overlap, the overlay wins, and the base's span is cut around it. So keywords, strings and comments come from the syntax layer and names from the server, and when the server is off, starting or has nothing for a line, the syntax layer alone shows.

- **Owns:** both highlighters.
- **Access:** public. Created by App when a file gets both layers. Main thread.
- **Required:** optional.
- **Failure modes:** none of its own; each layer's failures stay that layer's.
- **Depends on:** [Highlighter](./highlight.hpp.skel.md#class-highlighter)
- **Unknowns:** none

## function: overlay_spans

The cut that layers two span lists, shared by [LayeredHighlighter.spans_for_line](#function-spans_for_line) and EditorView's merge colors.

- **Inputs:** `base`, `over`: `const std::vector<StyleSpan>&`, each sorted and not overlapping.
- **Returns:** `base` with `over`'s ranges cut out of it and `over`'s spans put in their place, sorted and not overlapping; either list as it is when the other is empty.
- **State changes:** none.
- **Access:** public, any thread.
- **Referred by:** [EditorView.set_merge_colors](../ui/editor_view.hpp.skel.md#function-set_merge_colors)

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
- **Returns:** [overlay_spans](#function-overlay_spans) of the base's spans and the overlay's.
- **State changes:** whatever the two layers' own calls change.
- **Access:** EditorView.

### function: tick

- **Inputs:** `now`.
- **Returns:** the earlier of the two layers' deadlines. `visible_range_changed` is forwarded to both, and `status()` is the overlay's.
- **State changes:** runs both layers' timers.
- **Access:** App.
