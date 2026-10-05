---
role: product
unit: ./semantic_highlighter.hpp.skel.md
stamp: source e5960656, stand-in 56f2505e
---
# module: semantic_highlighter (implementation)

Implements [SemanticHighlighter](./semantic_highlighter.hpp.skel.md#class-semantichighlighter). It decodes the 5-integer relative tokens `(deltaLine, deltaStart, length, type, modifiers)` with the legend from `initialize`: the line is the previous token's plus `deltaLine`, and the start character is relative to the previous token's start only when `deltaLine` is 0. Character offsets become byte offsets on the token's line: directly for `utf-8`, and by walking code points for `utf-16` (2 units above U+FFFF, 1 otherwise, including each invalid byte), reading each line's bytes once while consecutive tokens stay on it. The legend's type names map to the `lsp_` styles by name; the modifier names `deprecated`, `readonly` and `documentation` map to the `kMod*` bits.

`before_change` computes the pre-edit start and end positions with the line index (and [utf16_length](../text/utf8.hpp.skel.md#function-utf16_length) over the line prefix for `utf-16`); `after_change` updates the line index and the cache, then calls `did_change` with those positions and the inserted text (`ChangeEvent.inserted_small`, or read from the document when larger), and arms the debounce. `reloaded` rebuilds the line index, clears the cache and calls `did_change_full`. `saved` calls `did_save`.

**Bursts.** Past `kBurstChanges` (64) changes before the next `tick` (a Replace All), keeping the line index, the cached spans and the server's copy in step change by change would cost O(lines + spans) each; the cached spans are dropped instead, nothing more is sent per change, and the next `tick` rebuilds the line index and sends the whole text once (`did_change_full`), then requests fresh tokens. **Decoding:** a line's tokens arrive in order, so UTF-16 columns are converted with a running position along the line, not from its start each time.

**Mapping the server's legend.** A token type with a standard name maps to its `lsp_` style. A type the standard does not have maps to the standard type whose name its own name ends with, compared without case, the longest such name winning: `selfParameter` and `clsParameter` become `parameter`, a `fooFunction` becomes `function`. A type that ends with no standard name maps to the default style and its tokens are not cached. Modifiers map by name to their bits: `deprecated`, `readonly`, `documentation`, `defaultLibrary`, and `declaration` (with `definition` treated as `declaration`); others are ignored. How each looks is the [theme](../ui/theme.hpp.skel.md)'s business, so no server's names appear in the theme.

- **Owns:** the decoder and the cache.
- **Access:** internal.
- **Required:** optional — as for the header.
- **Failure modes:** a token whose length runs past the end of its line is clamped to the line. A token on a line past the end of the document ends the decoding. A token that overlaps the previous one is trimmed to start after it, and dropped if nothing is left.
- **Depends on:** [SemanticHighlighter](./semantic_highlighter.hpp.skel.md#class-semantichighlighter)
- **Depends on:** [LspClient.request_tokens](./lsp_client.hpp.skel.md#function-request_tokens)
- **Depends on:** [utf16_length](../text/utf8.hpp.skel.md#function-utf16_length)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
