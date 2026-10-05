---
role: product
unit: ./syntax_highlighter.hpp.skel.md
stamp: source 33a1dcbb, stand-in bd926263
---
# module: syntax_highlighter (implementation)

Implements [SyntaxHighlighter](./syntax_highlighter.hpp.skel.md#class-syntaxhighlighter). Keyword and constant lists are kept as hash sets of words; the opening markers are tried longest first.

- **Owns:** nothing beyond the class.
- **Access:** internal.
- **Required:** optional — as the header.
- **Failure modes:** none beyond the header's.
- **Depends on:** [SyntaxHighlighter](./syntax_highlighter.hpp.skel.md#class-syntaxhighlighter)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
