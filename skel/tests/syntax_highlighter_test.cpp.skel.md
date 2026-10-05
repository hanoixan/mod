---
role: test
stamp: source d34420cf, stand-in 41845c35
---
# module: syntax_highlighter_test

`scan_line` with small specs: keywords and constants as whole words only, case-insensitive when asked; line comments, with and without `commentNeedsSpace`; block comments opening and closing on one line and across lines; strings with escapes, prefixes (only directly before the delimiter and not the end of a word), the longest delimiter first, an unclosed single-line string to the line's end, `maxLength` refusing a lifetime-like quote; multi-line strings carried across lines; numbers in their forms and not inside words; markers inside strings and comments ignored. Over a document: the state at a line found through checkpoints and the back-scan bound, checkpoints dropped after an edit before them, and `reloaded`. The built-in `syntax` entries of [config/languages.json](../config/languages.json.skel.md) all parse, and a sample line of each built-in language gets keyword, string, comment and number spans. LayeredHighlighter: overlay spans win and cut base spans, either layer alone passes through, events reach both layers, the earlier deadline and the overlay's status.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [SyntaxHighlighter](../src/syntax/syntax_highlighter.hpp.skel.md#class-syntaxhighlighter)
- **Depends on:** [LayeredHighlighter](../src/syntax/layered_highlighter.hpp.skel.md#class-layeredhighlighter)
- **Depends on:** [LanguageConfig](../src/syntax/language_config.hpp.skel.md#class-languageconfig)
- **Depends on:** [config/languages.json](../config/languages.json.skel.md)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
