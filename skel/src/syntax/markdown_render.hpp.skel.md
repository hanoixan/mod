---
role: product
stamp: source 4d1f4223, stand-in 52984766
---
# module: markdown_render

Lays a Markdown page out for reading: the help viewer, and a Markdown document in read-only mode, show what the Markdown **means**, never its marks. Pure: text and a width in, laid-out lines out; it is re-run when the width changes.

| Markdown | Rendered |
|---|---|
| `#` heading | its text in the heading's style, a blank line before; level 1 underlined with `═` and level 2 with `─` across the text's width |
| paragraph | its inline text, word-wrapped to the width, a blank line after |
| `**strong**`, `*emphasis*` or `_emphasis_`, `~~strike~~`, `` `code` `` | the text in `md_strong`, `md_emphasis`, `md_strike`, `md_code`; the marks are dropped; a backslash escape shows the escaped character |
| `[text](target)` | the text in `md_link_text`, recorded as a link to `target`; the target is never shown |
| `-`, `*` or `+` list item | `• ` and the text, wrapped with a hanging indent; nested items indented two more columns per level |
| `1.` list item | its number and `. ` the same way |
| `>` quote | `│ ` in `md_quote` before each line, the text in `md_quote` |
| fenced code (`` ``` `` or `~~~`) | each line indented by four columns in `md_code_block`, not wrapped, the fences dropped |
| table | columns aligned to their widest cell (cell text rendered inline, so marks and link targets do not count), ` │ ` between columns, and a `─┼─` rule under the header row; the alignment row is dropped |
| `---`, `***` or `___` alone | a rule of `─` across the width |

Links, emphasis, strong and strike nest at most 16 deep; deeper marks stay literal text, so no input can exhaust the stack. Every search for a closing mark is remembered when it fails, and brackets are paired once per block, so runs of unmatched marks take linear time.

Lines are measured in display columns (UTF-8, wide characters counted as two). A word longer than the width is broken at the width. Widths under 20 are treated as 20. The styles are the Markdown [Styles](./highlight.hpp.skel.md#symbol-style), so the [theme](../ui/theme.hpp.skel.md) decides the look, in color in xterm mode and in bold, underline and reverse in vt100 mode.

- **Owns:** nothing.
- **Access:** public. Pure functions.
- **Required:** optional — the help viewer and read-only Markdown use it.
- **Failure modes:** Markdown outside the subset above (HTML, footnotes, setext headings, reference links) is shown as plain text, marks and all.
- **Depends on:** [Style](./highlight.hpp.skel.md#symbol-style)
- **Depends on:** [heading_slug](./markdown.hpp.skel.md#function-heading_slug)
- **Unknowns:** none

## symbol: RenderedPage

`RenderedLine { std::string text; std::vector<RenderSpan> spans; std::uint32_t source_line; }` with `RenderSpan { std::size_t begin, end; Style style; }` (byte offsets into `text`, sorted, not overlapping) and `source` the **source map**: one entry per byte of `text`, the byte offset in the page's source of the byte it shows, or `kGenerated` (`std::uint64_t` max) for a byte the layout made up (a bullet, a quote bar, a rule, indentation, table padding and dividers). The bytes of a code point that shows source text map to consecutive offsets; an escaped character maps to itself, not its backslash; a space between words maps to the source's whitespace (or line break) it stands for. `source_line` is the 1-based line of the source the rendered line came from. `RenderedLink { std::string target; std::vector<LinkPiece> pieces; }` with `LinkPiece { std::size_t line, begin, end; }` (a link wrapped over lines has a piece on each). `RenderedPage { std::vector<RenderedLine> lines; std::vector<RenderedLink> links; std::vector<std::pair<std::string, std::size_t>> anchors; }`, the anchors being each heading's [slug](./markdown.hpp.skel.md#function-heading_slug) and its rendered line. Links are in reading order.

- **Access:** public.

## function: render_markdown

- **Inputs:** `text`: a whole Markdown page; `width`: the columns available.
- **Returns:** a `RenderedPage`.
- **State changes:** none.
- **Access:** [HelpViewer](../ui/help_viewer.hpp.skel.md#class-helpviewer), [ReadingLayout](../ui/reading_layout.hpp.skel.md#class-readinglayout), tests.
- **Referred by:** [markdown_render (implementation)](./markdown_render.cpp.skel.md)
- **Referred by:** [help_viewer](../ui/help_viewer.hpp.skel.md)
- **Referred by:** [markdown_render_test](../../tests/markdown_render_test.cpp.skel.md)
- **Referred by:** [reading_layout](../ui/reading_layout.hpp.skel.md)
