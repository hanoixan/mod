---
role: product
unit: ./markdown.hpp.skel.md
stamp: source 1fd60452, stand-in 4081d3f0
---
# module: markdown (implementation)

Implements [MarkdownHighlighter](./markdown.hpp.skel.md#class-markdownhighlighter): a block-state pass to find the fence state at the line start, then the line's block prefix (quote markers, then a thematic break, an ATX heading or a list marker), then an inline tokenizer over the rest of the line, painting a per-byte style array that is run-length encoded into spans.

The tokenizer is one left-to-right scan. Backslash escapes, code spans (a backtick run closed by a run of exactly the same length) and autolinks are consumed where they start, so their content is never tokenized further. `[` and `![` push a bracket; `]` followed by `(destination)` (balanced parentheses, or `<…>`) makes a link, whose text's delimiters are resolved within it, after which earlier `[` openers become literal (no links in links); a `]` with no destination makes its bracket literal. Delimiter runs of `*`, `_` and `~` are classified with the CommonMark left- and right-flanking rules, using [char_class](../text/utf8.hpp.skel.md#function-char_class) to tell Unicode whitespace and punctuation apart (line edges count as whitespace), with the `_` intraword restriction, and pushed on the delimiter stack. At the end of the line, CommonMark's "process emphasis" runs over the stack, with the rule of three and the `openers_bottom` table (by character, closer length mod 3, and whether the closer can open), using 2 delimiters (strong) when both runs have at least 2, else 1.

The outline scan ([scan_markdown](./markdown.hpp.skel.md#function-scan_markdown)) reuses the same tokenizer, the fence tracking and the line-prefix rules, so a link is a link in the outline exactly when it is styled as one. Reference links are recorded only for the outline: the tokenizer collects them when the outline asks and never styles them.

- **Owns:** the tokenizer.
- **Access:** internal.
- **Required:** optional — as for the header.
- **Failure modes:** quadratic behavior on lines with thousands of unmatched `*`. Cap the delimiter stack at 1024 (`kMaxDelimiters`) and treat the rest as literal; the bracket stack has the same cap.
- **Depends on:** [MarkdownHighlighter](./markdown.hpp.skel.md#class-markdownhighlighter)
- **Depends on:** [char_class](../text/utf8.hpp.skel.md#function-char_class)
- **Depends on:** [decode](../text/utf8.hpp.skel.md#function-decode)
- **Depends on:** [decode_before](../text/utf8.hpp.skel.md#function-decode_before)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
