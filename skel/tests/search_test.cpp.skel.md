---
role: test
stamp: source 9215e653, stand-in 4cce7c6f
---
# module: search_test

Matches across piece boundaries and window boundaries; backward search; wrap; whole-word and case options; `replace_all` as one undo node; cancel mid-replace-all; invalid pattern errors; line-bounding (`\s+`, `[^a]+`, `\R`, `a\nb` and `.*` never match across an LF or a CRLF, and `^`/`$` match at every line); Unicode-aware `\w`, `\b`, whole-word and case-insensitive matching on non-ASCII text; replacement templates `$1`, `${10}`, `${name}`, `$0`, `$$`, a non-participating group expanding to nothing, and errors for an unknown group number or name and an unterminated `${`; plain-text mode replaces with the literal template (`$5.00` stays `$5.00`); a line longer than 16 MiB is searched in pieces and reported; a multi-GB sparse file searched within the time-slice budget. Whole word: a term starting or ending with punctuation matches on its own, not glued to a word character. Replace All over one long line replaces every match; searching a 4 MB single line match after match, with a LineCursor, takes linear time. Match after match on one 4 MB line takes linear time with JIT and without (the interpreter checks each line's UTF-8 once), and without JIT a line with invalid UTF-8 is still searched, as with it.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [Searcher](../src/search/search.hpp.skel.md#class-searcher)
- **Depends on:** [Regex](../src/search/regex.hpp.skel.md#class-regex)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
