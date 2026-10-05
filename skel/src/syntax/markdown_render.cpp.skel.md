---
role: product
unit: ./markdown_render.hpp.skel.md
stamp: source 951ff723, stand-in 8385ce8f
---
# module: markdown_render (implementation)

Implements [render_markdown](./markdown_render.hpp.skel.md#function-render_markdown): a block pass over the source lines (headings, fences, lists, quotes, tables, rules, paragraphs), then an inline pass that turns each block's text into styled runs and links, then word-wrapping of the runs.

- **Owns:** nothing.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [render_markdown](./markdown_render.hpp.skel.md#function-render_markdown)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
