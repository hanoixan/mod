---
role: product
unit: ./regex.hpp.skel.md
stamp: source 8f8bbccf, stand-in e6fe3a8d
---
# module: regex (implementation)

Implements [Regex](./regex.hpp.skel.md#class-regex) on top of the PCRE2 8-bit API.

- `compile`: `pcre2_compile` with `PCRE2_UTF | PCRE2_UCP | PCRE2_MATCH_INVALID_UTF`, so classes, `\b` and case folding are Unicode-aware, and invalid UTF-8 in the document never makes a match fail with an error. It also reads the group count (`PCRE2_INFO_CAPTURECOUNT`) and the name-to-number table (`PCRE2_INFO_NAMECOUNT`, `PCRE2_INFO_NAMEENTRYSIZE`, `PCRE2_INFO_NAMETABLE`) once, for `expand_replacement`. `case_insensitive` adds `PCRE2_CASELESS`. `whole_word` wraps the pattern in `\b(?:…)\b`. `literal` adds `PCRE2_LITERAL` and drops `PCRE2_UCP` (PCRE2 refuses that combination, and it means nothing for a literal), except when `whole_word` is also set: then the pattern is escaped by hand before wrapping, because `PCRE2_LITERAL` would make the wrapper literal too. A compile error becomes `ErrorCode::regex` carrying PCRE2's message (`pcre2_get_error_message`) and the error offset. Then `pcre2_jit_compile(PCRE2_JIT_COMPLETE | PCRE2_JIT_PARTIAL_HARD)` is attempted. If JIT fails, for example under a W^X policy, the interpreter is used silently.
- `search_window`: for each line of the window, from the line containing `from` onward, one `pcre2_match` whose subject pointer is the line start and whose length stops before the line's LF (and a CR immediately before it), with `start_offset = from - line_start` on the first line and 0 after (a `from` inside a CR LF line break starts on the next line instead, so an empty match at the line end is never found twice); lookbehind can see bytes before `from` on the same line only. Line feeds are found with `memchr`. Pass `PCRE2_PARTIAL_HARD` only for the window's last line, and only when it is incomplete (no line feed before the window end) and not `at_eof`. `PCRE2_ERROR_PARTIAL` maps to `need_more`, `PCRE2_ERROR_NOMATCH` to `none`, and a full match to `Match` with offsets rebased by `window_offset`. Unset groups (`PCRE2_UNSET`) are recorded as the pair `(kUnsetGroup, kUnsetGroup)`, so that `groups[i]` is always group `i + 1` and `expand_replacement` can tell a non-participating group from a missing one.
- Budget: a match context with `pcre2_set_match_limit`, `pcre2_set_depth_limit` and `pcre2_set_heap_limit`. `PCRE2_ERROR_MATCHLIMIT`, `PCRE2_ERROR_DEPTHLIMIT` and `PCRE2_ERROR_HEAPLIMIT` map to `ErrorCode::regex` with "pattern too complex".
- `expand_replacement` is hand-written rather than `pcre2_substitute`, because the match lives in a window that may have moved on; it reads group bytes through the `read` callback and parses `$n`, `${n}`, `${name}` and `$$` as the header describes. A pattern compiled with `literal` returns the template unchanged.
- Match data comes from `pcre2_match_data_create_from_pattern` once per compiled pattern and is reused for every call.

- **Owns:** engine handles and match data.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** exceeding the match budget is reported as "pattern too complex". An empty match must advance the next search by one code point, not one byte, so the Searcher never loops or lands inside a UTF-8 sequence. JIT stack exhaustion (`PCRE2_ERROR_JIT_STACKLIMIT`) is treated as exceeding the budget.
- **Depends on:** [Regex](./regex.hpp.skel.md#class-regex)
- **Depends on:** [PCRE2 API](https://www.pcre.org/current/doc/html/pcre2api.html)
- **Depends on:** [PCRE2 partial matching](https://www.pcre.org/current/doc/html/pcre2partial.html)
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)

- **Unknowns:** none
