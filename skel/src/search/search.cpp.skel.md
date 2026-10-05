---
role: product
unit: ./search.hpp.skel.md
stamp: source eaee50de, stand-in 7361f842
---
# module: search (implementation)

Implements [Searcher](./search.hpp.skel.md#class-searcher). Windowing: start at the line start at or before `from`, gather bytes into a reusable 4 MiB window buffer that is copied out of the pieces, and call `Regex.search_window`. On `need_more`, grow the window up to the 16 MiB cap. Otherwise slide forward to the line start after the last fully examined line. A line longer than the cap is searched in consecutive cap-sized pieces, each starting where the previous one ended (mid-line, flagged `line_too_long`), so a step never scans more than about one cap past its budget; every line-feed search the windowing does (finding the line start before `from`, the line end after a backward limit) is bounded by the cap for the same reason. A backward search reads windows ending at a line boundary at or after its limit, runs forward through each, and keeps the last match that starts before the limit.

- **Owns:** the window buffer.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a match that crosses a window boundary is missed. The window always starts on a line boundary and grows on `need_more`, which prevents this up to the cap. Because matches never span a line break, only the window's last, incomplete line can ever need more bytes.
- **Depends on:** [Searcher](./search.hpp.skel.md#class-searcher)
- **Depends on:** [Regex.search_window](./regex.hpp.skel.md#function-search_window)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
