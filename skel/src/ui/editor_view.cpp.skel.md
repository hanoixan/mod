---
role: product
unit: ./editor_view.hpp.skel.md
stamp: source e4356208, stand-in 97bbacd5
---
# module: editor_view (implementation)

Implements [EditorView](./editor_view.hpp.skel.md#class-editorview). For each row, it walks from the previous row's line start with `find_lf_forward`, or, with word wrap on, from the previous row with [WrapLayout](../text/wrap.hpp.skel.md#class-wraplayout). One routine draws a byte range of a line on a screen row for both layouts and reports whether text was left over past the right edge, which is what puts the `>` marker on an unwrapped row; highlighter spans are fetched once per line and reused for all its rows. It decodes bytes into cells, applying the highlighter spans, then the search match overlay, then the selection overlay. Control characters render as `^X`, C1 controls (U+0080–U+009F) as `\u0080`-style escapes (six cells), and invalid bytes as `\xNN`, all in the `md_markup`/dim style. The cell counts are those of [display_width](../text/utf8.hpp.skel.md#function-display_width). A CR before an LF is skipped.

- **Owns:** the per-frame scratch vectors, reused between frames.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** gutter line numbers for rows below `top` are derived by adding 1 per row from `line_of(top)`, so only one tree query is made per frame. If that query returns unknown, the whole gutter shows `…`.
- **Depends on:** [EditorView](./editor_view.hpp.skel.md#class-editorview)
- **Depends on:** [PieceTree.line_of](../text/piece_tree.hpp.skel.md#function-line_of)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
