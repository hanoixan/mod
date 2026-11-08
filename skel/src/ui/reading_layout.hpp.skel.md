---
role: product
stamp: source c6202ad8, stand-in fe78d75a
---
# module: reading_layout

A Markdown document laid out for reading in view mode ([render_markdown](../syntax/markdown_render.hpp.skel.md#function-render_markdown)), with the arithmetic that keeps the editor's cursor, selection, Find and links working on the source while the screen shows the laid-out text. Pure, so it is tested on its own; [EditorView](./editor_view.hpp.skel.md#class-editorview) holds one while it draws a document this way.

A **position** is a place the cursor can be shown: the start of each code point that shows source text, and the end of each line that has any (just past its last such code point). Each has a source offset, a rendered line, a byte in that line and a display column. A line with none (a blank line, a heading's underline, a rule) is passed over by the cursor.

- **Owns:** the rendered page and its positions.
- **Access:** public. Main thread.
- **Required:** optional — Markdown in view mode only.
- **Failure modes:** none: an offset no position has (a hidden mark, the inside of a link's target) shows at the next position after it, or the last.
- **Depends on:** [render_markdown](../syntax/markdown_render.hpp.skel.md#function-render_markdown)
- **Depends on:** [Motion](../edit/editor.hpp.skel.md#symbol-motion)
- **Unknowns:** none

## class: ReadingLayout

- **Inputs:** `text` (the whole document), `width` (at least 20 columns) and `wrap` (default false; passed to render_markdown), laid out at construction.
- **State changes:** none after construction; a new width or a change to the text makes a new one.
- **Owns:** see the module.
- **Access:** EditorView, App (through EditorView), tests.
- **Referred by:** [App.handle_external_change](../app/app.hpp.skel.md#function-handle_external_change)
- **Referred by:** [EditorView.render](./editor_view.hpp.skel.md#function-render)
- **Referred by:** [reading_layout (implementation)](./reading_layout.cpp.skel.md)
- **Referred by:** [reading_layout_test](../../tests/reading_layout_test.cpp.skel.md)
- **Referred by:** [fuzz_markdown](../../fuzz/fuzz_markdown.cpp.skel.md)

### function: page

- **Inputs:** none.
- **Returns:** the `RenderedPage`.
- **State changes:** none.
- **Access:** EditorView.

### function: locate

- **Inputs:** `offset`: a source offset.
- **Returns:** `Spot { line; byte; col; }`: the position at `offset`, else the first after it, else the last; `{0, 0, 0}` for a page with none.
- **State changes:** none.
- **Access:** EditorView (the cursor, scrolling), App.

### function: move

- **Inputs:** `motion`: a [Motion](../edit/editor.hpp.skel.md#symbol-motion); `cursor`: the source offset it starts from; `page_lines`: rendered lines per page; `sticky`: the column Up and Down aim for, kept by the caller.
- **Returns:** the source offset of the position the motion reaches. Left and Right (and the word motions) go to the previous or next position in source order; Up and Down to the nearest position at or before the aimed column on the previous or next line that has positions; PageUp and PageDown `page_lines` lines at once, then on to the nearest line with positions; LineStart and LineEnd to the first and last position of the cursor's line; DocStart and DocEnd to the first and last position.
- **State changes:** `sticky` is set to the column a vertical motion aimed for and cleared by any other motion. `VisualLineStart` and `VisualLineEnd` are `LineStart` and `LineEnd` here: a rendered line is what is shown.
- **Access:** App, for the motion commands while the view draws this way.

### function: offset_at

- **Inputs:** `line`: a rendered line; `col`: a display column on it.
- **Returns:** `std::optional<uint64_t>`: the source position shown at `col`, the nearest at or before it (the line's first when `col` is before it). A line with no positions (a blank line, a rule) gives the last position of the nearest line above with any, else the first of the nearest below; nullopt for a line past the page or an empty page.
- **State changes:** none.
- **Access:** EditorView, for a mouse click on text laid out for reading.

### function: visible_text

- **Inputs:** `start`, `end`: a source range.
- **Returns:** the text shown for it: the bytes whose source offset is in the range, with the made-up bytes between two of them on a line, and a line's leading made-up bytes (a bullet, a quote bar, indentation) when its first source byte is in the range. Lines from one source block are joined by a space, others by a line break.
- **State changes:** none.
- **Access:** App's copy, when the `read_only_copy` setting is `visible text`.
