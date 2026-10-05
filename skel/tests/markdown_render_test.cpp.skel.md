---
role: test
stamp: source e74fe91a, stand-in f97134f1
---
# module: markdown_render_test

Headings (levels, underlines, slugs as anchors, blank lines), paragraphs wrapped at the width with words kept whole and an over-long word broken, inline strong, emphasis, strike, code and escapes with their marks gone and their styles set, links shown as their text only with targets recorded and pieces across a wrap, bullet and numbered lists with hanging indents and nesting, quotes, fenced code kept verbatim and indented, tables aligned with the header rule and inline marks not counted, rules, `source_line` of each line, and that no rendered line of any page of the real manual contains `](`, a line starting with `#`, or a fence. The source map: emphasis, a heading, a link, inline code, an escape, a list, a quote, a fence, a table and a wrapped paragraph map their shown bytes to the source and their made-up bytes to kGenerated. Hostile input: deeply nested links and emphasis, runs of unmatched brackets, parens and backticks lay out in linear time without exhausting the stack.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [render_markdown](../src/syntax/markdown_render.hpp.skel.md#function-render_markdown)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
