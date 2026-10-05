---
role: product
stamp: source e87965c1, stand-in 2b4fd836
---
# module: regex

A thin wrapper that isolates the regex engine, [PCRE2](https://www.pcre.org/current/doc/html/) (8-bit library, fetched at a pinned version; see [CMakeLists.txt](../../CMakeLists.txt.skel.md)). PCRE2 was chosen because its partial matching (`PCRE2_PARTIAL_HARD`) gives exactly the `need_more` answer that streaming needs, it has match and depth limits for a backtracking budget, and it is BSD-licensed. No PCRE2 type appears in this header, so callers never include `pcre2.h`. The engine must search a document that is *not contiguous* (pieces) and may be larger than RAM, so the contract is chunked: matching runs over a sliding window of bytes, and a match is only reported once the engine is sure it cannot extend further.

#### Flavor exposed to users

- **Syntax:** PCRE2's pattern syntax, compiled with `PCRE2_UTF`.
- **Unicode-aware classes and case:** `\w`, `\d`, `\s`, `\b` and the POSIX classes use Unicode properties (`PCRE2_UCP`), and case-insensitive matching uses Unicode simple case folding (`PCRE2_CASELESS` under `PCRE2_UTF`), so `\w` matches `é` and `É` matches `é` case-insensitively. Full case folding (`ß` against `SS`) is not done. The whole-word option wraps the pattern in `(?<!\w)(?:…)(?!\w)` (not next to a word character on either side), which is Unicode-aware too and, unlike `\b`, lets a term that starts or ends with punctuation (`foo(`, `-x`) match on its own.
- **Line-bounded:** a match **never spans a line break**, whatever the pattern. A line break is an LF together with a CR immediately before it. `.`, `\s`, `\R`, negated classes such as `[^a]`, and a literal `\n` in the pattern can never consume one, because each line is matched as a separate subject that does not contain its line break (see [search_window](#function-search_window)). `^` and `$` therefore match at every line start and line end. A pattern that can only match by crossing a line break, such as `a\nb`, simply finds nothing. This bounds the window to "current line plus lookahead", and it is what lets search stream.
- **Replacement templates:** `$1` … `$99` and `${1}` refer to numbered groups, `${name}` to a named group, `$0` or `${0}` to the whole match, and `$$` to a literal `$`. Digits after `$` are read greedily (at most two), so `${1}0` is the way to write group 1 followed by `0`. Every other character, including `\`, is literal. Templates are expanded **only in regex mode**: when the find bar's regex option is off (the pattern was compiled with `literal`), the replacement is literal text, so `$5.00` replaces with `$5.00` and `$1` is not an error. See [expand_replacement](#function-expand_replacement).

- **Owns:** the compiled pattern.
- **Access:** public. Used only by [Searcher](./search.hpp.skel.md#class-searcher).
- **Required:** always.
- **Failure modes:** an invalid pattern gives `ErrorCode::regex`, shown inline in the find bar. Catastrophic backtracking on adversarial patterns: the engine must offer a match budget or be non-backtracking. A line of many GB exceeds the window: the window is capped at 16 MiB, and the find bar reports "line too long, match may be missed".
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)

- **Unknowns:** none

## symbol: Match

`{ uint64_t start; uint64_t end; std::vector<std::pair<uint64_t,uint64_t>> groups; }`. Offsets are absolute document byte offsets.

- **Access:** public.

## class: Regex

- **Inputs:** `pattern`: a UTF-8 string; `options`: `{ case_insensitive, literal (no regex), whole_word }`.
- **State changes:** immutable after compilation.
- **Owns:** the compiled PCRE2 pattern (`pcre2_code_8`), its match data block and match context, all released in the destructor. Move-only.
- **Access:** main thread.
- **Referred by:** [regex (implementation)](./regex.cpp.skel.md)
- **Referred by:** [search](./search.hpp.skel.md)
- **Referred by:** [search_test](../../tests/search_test.cpp.skel.md)
- **Referred by:** [fuzz_regex](../../fuzz/fuzz_regex.cpp.skel.md)

### function: compile

- **Inputs:** `pattern`, `options`.
- **Returns:** `Result<Regex>`.
- **State changes:** none.
- **Access:** Searcher, when the find text changes. The result is cached until the text changes.

### function: search_window

- **Inputs:** `window`: contiguous bytes that start at a line start; `window_offset`: the window's absolute offset; `from`: the absolute offset to start from; `at_eof`: whether the window ends at the end of the document; `cursor`: an optional `LineCursor { valid; searched_to; line; line_feed; }` a caller keeps while it searches one window again and again from ever later points: the line holding `from` is then found by looking back only as far as the last call, and a call still on the same line reuses its end, so collecting every match of one long line costs linear, not quadratic, time.
- **Returns:** the first `Match` starting at or after `from` and fully inside one line of the window; `need_more`, if the window's last line is not complete (no line feed before the window end and `!at_eof`) and a match in it might continue past the window end; or `none`.

The window is split at line feeds, and each line is matched as its own subject: the subject runs from the line start to just before its line break (excluding the LF and a CR immediately before it), so no match can include a line break and lookbehind cannot see the previous line. Lines are tried in order, starting with the line that contains `from`. Only the last line of the window can be incomplete; partial matching applies to it alone.
- **State changes:** updates `cursor` when one is given.
- **Access:** Searcher.
- **Referred by:** [search (implementation)](./search.cpp.skel.md)

### function: expand_replacement

- **Inputs:** `match`; `template_`: the replacement text, with `$1`, `${1}`, `${name}`, `$0` and `$$` references as described in the flavor section above; `read`: a callback that fetches the group bytes.
- **Returns:** `Result<std::string>`: the replacement bytes. For a `Regex` compiled with `literal` (a plain-text search), `template_` is returned unchanged and never fails: there is no `$` processing at all. Otherwise: a reference to a group that did not take part in the match expands to nothing. A reference to a group number or name the pattern does not have, or a malformed reference (`${` with no closing brace, `$` followed by nothing valid), is `ErrorCode::regex` with a message naming it, such as "no group named 'year'"; it is reported in the find bar and nothing is replaced. Names are resolved through the pattern's name table, captured at compile time.
- **State changes:** none.
- **Access:** Searcher.replace_*.
