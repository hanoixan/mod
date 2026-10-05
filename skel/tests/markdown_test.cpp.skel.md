---
role: test
stamp: source fd0b092b, stand-in a025abf4
---
# module: markdown_test

Span golden tests for each supported construct (headings, emphasis and strong with the flanking rules, strike, code spans, links, images, autolinks, escapes, block quotes, list markers, thematic breaks, backtick and tilde fences, an unclosed fence, CR LF line ends); fence state across checkpoints after edits (removing an opening fence, adding one, changing its character); the 4 MiB bound on the back-scan, untrusted checkpoints after a jump, and the correct state after scrolling through, both by jumps and line by line; the delimiter-stack cap; no spans crossing lines, checked for every line queried. Documents are real files in the scratch directory opened with `Document.open`, and the fixture registers the highlighter as a listener. Hostile input: a line of `[](` repeated and hundreds of thousands of autolinks style and scan in linear time, with the link count capped.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [MarkdownHighlighter](../src/syntax/markdown.hpp.skel.md#class-markdownhighlighter)
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Depends on:** [StyleSpan](../src/syntax/highlight.hpp.skel.md#symbol-stylespan)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
