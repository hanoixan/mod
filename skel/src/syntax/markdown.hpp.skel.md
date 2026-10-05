---
role: product
stamp: source 0779cdbd, stand-in 3274b12c
---
# module: markdown

In-place Markdown styling. The markup characters remain visible and editable and are shown dimmed. Their content gets the semantic style: heading colors, *italic* for emphasis, **bold** for strong, a code color, and underlined link text. Hiding the markup was rejected: it breaks the one-to-one mapping between display columns and bytes that cursor movement relies on, and an editor must show what will be saved.

Supported syntax (a CommonMark subset): ATX headings, emphasis and strong with `*` or `_`, `~~strike~~` (GFM: runs of one or two tildes, closed by a run of the same length), inline code, fenced code blocks (backtick and tilde fences), block quotes, list markers, inline links `[text](url)` and images `![alt](src)` (styled the same way), autolinks `<url>` and `<email>`, backslash escapes, and thematic breaks. Setext headings, reference links, HTML and tables are not styled.

Because only fence state is carried between lines, container structure is approximated per line: fences, headings, block quotes and thematic breaks are recognized at an indentation of at most 3 spaces (after any `>` markers for headings); fences inside block quotes or list items are not recognized; list markers are recognized at any indentation, so nested lists are styled; and indented code blocks are not styled. A line longer than 1 KiB is never a closing fence (only its first 1 KiB is kept while scanning). A trailing CR before the LF is never styled.

What each construct paints: fence lines are `md_markup` and lines inside a fence `md_code_block`; `#` runs, the optional closing `#` run, `>` markers, emphasis delimiters, backticks, brackets and parentheses of links, the angle brackets of autolinks, escaping backslashes and whole thematic breaks are `md_markup`; list markers are `md_list_marker`; heading content (after the `#` run) is `md_headingN`; quote content (after the marker and one optional space) is `md_quote`; link and image text is `md_link_text` and the destination `md_link_url`. Nested styles are flattened so the innermost wins: a heading or quote is the base, inline constructs paint over it, emphasis and link text nest by extent, code spans and URLs paint over those, and markup paints last. Adjacent runs of the same style are one span.

- **Owns:** the block-state checkpoint cache.
- **Access:** public. The highlighter is created by App for Markdown documents; the outline functions are free and stateless.
- **Required:** optional — without it Markdown files show plain text.
- **Failure modes:** an unclosed fence makes everything after it a code block, which matches CommonMark. In a huge file, a jump to the end needs the fence state of all preceding text: scanning from the nearest checkpoint is bounded to 4 MiB backwards, and past that the highlighter assumes "not in fence". It can be wrong until the user scrolls through the region, which is accepted. Emphasis delimiter rules are complex: implement the CommonMark left- and right-flanking rules within a single line only, so emphasis never spans lines.
- **Depends on:** [Highlighter](./highlight.hpp.skel.md#class-highlighter)
- **Depends on:** [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read)
- **Unknowns:** none

## symbol: MarkdownLink

`{ uint64_t start, end; uint64_t text_start, text_end; std::string target; }`: a link with absolute byte offsets. `start`/`end` cover the link as written (`[text](target)`, `[text][ref]`, `<https://…>`); `text_start`/`text_end` the text that shows (for an autolink, the address); `target` the destination with backslash escapes removed and a reference resolved to its definition's destination. A title after the destination is not part of it.

- **Access:** public.

## symbol: MarkdownHeading

`{ uint64_t start; std::string slug; }`: the offset of a heading's first line and its anchor slug.

- **Access:** public.

## symbol: MarkdownOutline

`{ std::vector<MarkdownLink> links; std::vector<MarkdownHeading> headings; bool truncated; }`, in document order. `truncated` is set when the document is longer than the scan went, or has more than `kMaxOutlineLinks` (100 000) links, the rest being left out. `kMaxOutlineBytes` is 16 MiB. The inline scanner pairs parentheses once per line and remembers failed searches (a backtick run's closer, a reference's `]`; a `<destination>` is looked for within 4096 bytes), so no line costs more than linear time.

- **Access:** public.
- **Referred by:** [read_only](../app/read_only.hpp.skel.md)

## function: scan_markdown

- **Inputs:** `text`: a `const PieceTree&`; `max_bytes`: how far to scan, `kMaxOutlineBytes` by default.
- **Returns:** the [MarkdownOutline](#symbol-markdownoutline) of the whole lines that end within `max_bytes`.
- **State changes:** none. It reads the text line by line, tracking fence state as the highlighter does, so nothing inside a fenced code block counts. Links are found with the highlighter's own inline tokenizer (inline links and images, autolinks), plus reference links (`[text][label]`, `[label][]` and `[label]`), which resolve against `[label]: destination` definition lines anywhere in the scanned text, labels matched without regard to case or runs of spaces; an unresolved reference is plain text and is not listed. Definition lines are not scanned for links. Headings are ATX headings (1 to 6 `#` followed by a space or the end of the line, closing `#`s dropped) and setext headings (a paragraph line followed by a line of only `=` or only `-`). A second heading with the same slug gets `-1`, the third `-2`, and so on, as GitHub numbers them.
- **Access:** App, for read-only mode.
- **Failure modes:** a document over 16 MiB is outlined only up to there, and its later links cannot be reached with Tab.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [markdown_links_test](../../tests/markdown_links_test.cpp.skel.md)
- **Referred by:** [read_only_test](../../tests/read_only_test.cpp.skel.md)
- **Referred by:** [docs_test](../../tests/docs_test.cpp.skel.md)
- **Referred by:** [fuzz_markdown](../../fuzz/fuzz_markdown.cpp.skel.md)

## function: heading_slug

- **Inputs:** a heading's text.
- **Returns:** GitHub's anchor for it: letters lower-cased (ASCII, Latin-1, Greek and Cyrillic capitals; other scripts are kept as they are), each space turned into `-`, `-` and `_` kept, and all other punctuation removed. So "Keys & Clipboard: Ctrl+K!" becomes `keys--clipboard-ctrlk`.
- **State changes:** none.
- **Access:** `scan_markdown` and tests.
- **Referred by:** [markdown_links_test](../../tests/markdown_links_test.cpp.skel.md)
- **Referred by:** [markdown_render](./markdown_render.hpp.skel.md)

## class: MarkdownHighlighter

- **Inputs:** `doc`: a `const Document&`.
- **State changes:** keeps a sparse map from line-start offset to block state (`MarkdownBlockState { bool in_fence; char fence_char; uint32_t fence_len; }`). Checkpoints are recorded every 256 lines (`kCheckpointLines`) as lines are scanned. The state at a line start depends only on the text before it, so `after_change` drops every checkpoint *after* the edit offset and keeps the ones at or before it; nothing needs shifting, because no kept checkpoint lies after the edit. It also keeps the state at the start of the line after the last one highlighted, so consecutive lines cost O(1); that memo is dropped when it lies after the edit offset. `reloaded` clears both. `checkpoints()` exposes the map for tests.
- **Owns:** checkpoints.
- **Access:** through `Highlighter`.
- **Referred by:** [app (implementation)](../app/app.cpp.skel.md)
- **Referred by:** [markdown (implementation)](./markdown.cpp.skel.md)
- **Referred by:** [markdown_test](../../tests/markdown_test.cpp.skel.md)

### function: spans_for_line

- **Inputs:** as in [Highlighter.spans_for_line](./highlight.hpp.skel.md#function-spans_for_line).
- **Returns:** sorted, non-overlapping spans within the line.
- **State changes:** may add checkpoints. The state at `line_start` comes from the memo, else from the nearest checkpoint at or before it (or the document start), scanning forward line by line with [PieceTree.read](../text/piece_tree.hpp.skel.md#function-read); when that is more than 4 MiB (`kMaxBackScan`) back, scanning starts at the first line start within the last 4 MiB with no open fence assumed. Checkpoints carry a `trusted` flag (`MarkdownCheckpoint { MarkdownBlockState state; bool trusted; }`, at namespace scope and aliased in the class as `BlockState` and `Checkpoint`): those recorded during an assumed scan are untrusted. When the nearest checkpoint is untrusted and a trusted one (or the document start) lies within 4 MiB, the scan starts from that instead and the untrusted checkpoints it passes are erased. Rendering consecutive lines through the memo also records a checkpoint every 256 lines, replacing a guess with a trusted state. So a wrong guess lasts only until the user scrolls through the region, by jumps of less than 4 MiB or line by line. Whether the line ends at an LF (or the end of the document), rather than being truncated, is checked with one `byte_at`.
- **Access:** EditorView.
