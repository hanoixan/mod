---
role: test
stamp: source 7168fdcd, stand-in edaeb392
---
# module: markdown_links_test

The outline scan: inline links with their whole extent, visible text and destination (a title excluded); destinations in angle brackets, with escapes, with nested parentheses, and images; autolinks, and what is not a link (code spans, an escaped bracket, an unclosed one); reference links in their three forms resolving against a definition later in the document, case-insensitively, and an unresolved one left out; nothing inside a fenced block counts as a link or a heading; ATX and setext headings with their slugs, duplicates numbered, a `#` without a space not a heading, letters beyond ASCII kept; absolute offsets across LF and CR LF lines; the scan cap leaving out what is past it and marking the outline truncated.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [scan_markdown](../src/syntax/markdown.hpp.skel.md#function-scan_markdown)
- **Depends on:** [heading_slug](../src/syntax/markdown.hpp.skel.md#function-heading_slug)
- **Depends on:** [PieceTree](../src/text/piece_tree.hpp.skel.md#class-piecetree)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
