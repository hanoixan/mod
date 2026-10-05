---
role: product
stamp: source c61924f6, stand-in 7f935b6b
---
# module: utf8

UTF-8 decoding, extended grapheme cluster boundaries (for cursor movement and deletion), terminal display width, word-boundary classes, and UTF-16 length (for LSP positions). It is lenient: invalid bytes are decoded as single-byte "invalid" units, so arbitrary binary data round-trips byte for byte.

- **Owns:** the declarations only.
- **Access:** public, pure free functions, callable from any thread.
- **Required:** always.
- **Failure modes:** a code point split across two pieces. Callers that walk pieces must carry up to 3 bytes of lookahead or use a read that stitches pieces together; see [PieceTree.read](./piece_tree.hpp.skel.md#function-read). A grapheme cluster can span pieces too, and has no length limit in Unicode (a base character followed by thousands of combining marks is valid), so cluster functions work on a caller-supplied window and say when they need more bytes. Terminals disagree with Unicode on the width of some emoji and ambiguous-width characters, and many draw a whole emoji ZWJ sequence in 2 cells where the per-code-point sum is larger, which misaligns the cursor. The editor follows the table and accepts the mismatch.
- **Unknowns:** none
- **Referred by:** [editor](../edit/editor.hpp.skel.md)

## symbol: Decoded

`{ char32_t cp; uint8_t len; bool valid; }`. When `valid` is false, `len == 1` and `cp` holds the raw byte value.

- **Access:** public.

## function: decode

- **Inputs:** `bytes`: a span starting at the position to decode. It may be shorter than the sequence needs.
- **Returns:** `Decoded`. Overlong forms, surrogates, values above U+10FFFF and truncated sequences are invalid. An empty span gives `len == 0`.
- **State changes:** none.
- **Access:** public.
- **Referred by:** [input (implementation)](../ui/input.cpp.skel.md)
- **Referred by:** [json (implementation)](../syntax/json.cpp.skel.md)
- **Referred by:** [markdown (implementation)](../syntax/markdown.cpp.skel.md)
- **Referred by:** [keymap (implementation)](../app/keymap.cpp.skel.md)
- **Referred by:** [fuzz_utf8](../../fuzz/fuzz_utf8.cpp.skel.md)

## function: decode_before

- **Inputs:** `bytes`: a span that *ends* at the position; at most 4 bytes are inspected.
- **Returns:** the `Decoded` value that ends exactly at the end of `bytes`. Used by `prev_grapheme_boundary` and for snapping a position to a code-point boundary.
- **State changes:** none.
- **Access:** public.
- **Referred by:** [markdown (implementation)](../syntax/markdown.cpp.skel.md)
- **Referred by:** [keymap_view (implementation)](../ui/keymap_view.cpp.skel.md)

## symbol: ClusterResult

`struct { enum { found, need_more } kind; uint32_t length; }`. With `found`, `length` is the cluster's byte length, always ≥ 1 for non-empty input and always a whole number of decoded units (see `decode`).

- **Access:** public.

## function: next_grapheme_boundary

Finds the end of the extended grapheme cluster that starts at the beginning of a window, using the rules of [UAX #29](https://www.unicode.org/reports/tr29/) (GB1–GB999, including GB9c for Indic conjuncts and GB11 for emoji ZWJ sequences) for the Unicode version of [width_table.inc](./width_table.inc.skel.md).

- **Inputs:** `bytes`: a window that starts at a cluster boundary; `at_end`: whether the window ends at the end of the document.
- **Returns:** `ClusterResult`. `need_more` when the window ends before the cluster is decided and `!at_end`; the caller widens the window and calls again. CR LF is one cluster (GB3), which matches the rule that the cursor never sits between CR and LF. Each invalid byte is a cluster of its own, treated as `Control`, so binary data still moves one byte at a time. A cluster longer than [MAX_CLUSTER_CODE_POINTS](#symbol-max_cluster_code_points) ends at the cap, on a code-point boundary.
- **State changes:** none.
- **Access:** public. [Editor](../edit/editor.hpp.skel.md#class-editor) for Right and Delete, and for word motions, which step over whole clusters.
- **Depends on:** [width table](./width_table.inc.skel.md)
- **Referred by:** [Editor](../edit/editor.hpp.skel.md)
- **Referred by:** [utf8 (implementation)](./utf8.cpp.skel.md)
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [wrap](./wrap.hpp.skel.md)
- **Referred by:** [text_field](../ui/text_field.hpp.skel.md)
- **Referred by:** [fuzz_utf8](../../fuzz/fuzz_utf8.cpp.skel.md)

#### Cluster length cap

Clusters are capped at [MAX_CLUSTER_CODE_POINTS](#symbol-max_cluster_code_points) = 32 code points. Without a cap, one Right or Backspace over a pathological run of combining marks is O(run length), and that run can be gigabytes. A longer run is split at the cap, on a code-point boundary, so the cursor can land inside such a run; that is accepted. The window the caller passes never needs to exceed 32 × 4 = 128 bytes to decide a capped cluster, plus whatever context `prev_grapheme_boundary` needs to find a certain boundary (see there).

## symbol: MAX_CLUSTER_CODE_POINTS

`inline constexpr uint32_t MAX_CLUSTER_CODE_POINTS = 32;`. The most code points one extended grapheme cluster may hold for cursor movement and deletion. Chosen in the spirit of the 30-non-starter limit of the Stream-Safe Text Format in [UAX #15](https://www.unicode.org/reports/tr15/). Each invalid byte counts as one code point.

- **Access:** public. Used by both boundary functions and by [Editor](../edit/editor.hpp.skel.md#class-editor) to size the window it reads.
- **Referred by:** [editor (implementation)](../edit/editor.cpp.skel.md)

## function: prev_grapheme_boundary

The mirror of `next_grapheme_boundary`: finds the start of the cluster that ends at the end of a window.

- **Inputs:** `bytes`: a window that ends at a cluster boundary; `at_start`: whether the window starts at the start of the document.
- **Returns:** `ClusterResult`, where `length` is the byte length of the cluster that ends at the end of the window. `need_more` when the rules need more preceding context and `!at_start`. Regional-indicator pairs (flags) and emoji ZWJ sequences need context reaching back to the start of the run, so the implementation scans back to a position where a boundary is certain (a `Control`, `CR` or `LF`, the window start with `at_start`, or the cap), then runs the forward rules. A boundary is certain between two code points when the pair rules break there and none of the context rules (GB9c, GB11, GB12/13) could join them. If no certain boundary is found within 64 code points (twice the cap, an even number so that regional-indicator pairs keep their alignment), that position is taken as the boundary. The result must equal what repeated `next_grapheme_boundary` calls from that position give, so it agrees with forward iteration from the document start whenever the run without a certain boundary is shorter than 64 code points. Fewer than 4 bytes of context before an invalid byte, with `!at_start`, is `need_more`, because the window may have cut a multi-byte sequence.
- **State changes:** none.
- **Access:** public. [Editor](../edit/editor.hpp.skel.md#class-editor) for Left and Backspace.
- **Depends on:** [width table](./width_table.inc.skel.md)
- **Referred by:** [Editor](../edit/editor.hpp.skel.md)
- **Referred by:** [utf8 (implementation)](./utf8.cpp.skel.md)
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [text_field](../ui/text_field.hpp.skel.md)

## function: display_width

- **Inputs:** `d`: a `Decoded`; `column`: the current display column (`uint64_t`), needed for tab stops; `tab_width`: the tab stop interval in columns, 1 to 16, 4 by default (the `tab_width` [setting](../app/settings.hpp.skel.md#function-setting_specs), changed in Options > User Settings…).
- **Returns:** the width in cells:
  - Tab advances to the next multiple of `tab_width`: `tab_width - column % tab_width`, so between 1 and `tab_width`.
  - Other C0 controls and DEL show as `^X`, width 2.
  - C1 controls (U+0080–U+009F, valid code points of category Cc) show as a `\u0080`-style escape: a backslash, `u`, and the code point as four hex digits, width 6. The table gives them width 0, so this branch comes before the table lookup. Sending them to the terminal raw is never done, because some act as control sequences (U+009B is CSI). The `\xNN` form is kept for invalid bytes, so the two stay distinguishable: the bytes `C2 9B` show as a six-cell `\u` escape and a lone `9B` byte as a four-cell `\x` escape.
  - Invalid bytes show as `\xNN`, width 4.
  - Combining marks and zero-width characters have width 0.
  - East Asian Wide/Fullwidth and emoji presentation characters have width 2, using [width_table.inc](./width_table.inc.skel.md).
  - Everything else has width 1.
- **State changes:** none.
- **Access:** public. This is a hot path, so the lookup is a binary search over a `constexpr` range array.
- **Depends on:** [width table](./width_table.inc.skel.md)
- **Referred by:** [editor (implementation)](../edit/editor.cpp.skel.md)
- **Referred by:** [utf8 (implementation)](./utf8.cpp.skel.md)
- **Referred by:** [editor_view](../ui/editor_view.hpp.skel.md)
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [wrap](./wrap.hpp.skel.md)
- **Referred by:** [file_dialog (implementation)](../ui/file_dialog.cpp.skel.md)
- **Referred by:** [text_field (implementation)](../ui/text_field.cpp.skel.md)
- **Referred by:** [scripted_terminal](../../tests/scripted_terminal.hpp.skel.md)

## symbol: CharClass

`enum class CharClass { space, word, punct, newline }`. `word` is Unicode letters and digits plus `_`. Non-ASCII code points count as `word` unless they are in the table's punctuation and space ranges. The table's `control` class maps as follows: LF and CR are `newline`; tab, vertical tab and form feed are `space`; other controls are `punct`. Invalid bytes are `punct`.

- **Access:** public.

## function: char_class

- **Inputs:** `d`: a `Decoded`.
- **Returns:** a `CharClass`.
- **State changes:** none.
- **Access:** [Editor](../edit/editor.hpp.skel.md#function-move) word motions (Ctrl+Left/Right).
- **Referred by:** [editor_test](../../tests/editor_test.cpp.skel.md)
- **Referred by:** [markdown (implementation)](../syntax/markdown.cpp.skel.md)
- **Referred by:** [wrap](./wrap.hpp.skel.md)

## function: utf16_length

- **Inputs:** `bytes`.
- **Returns:** the number of UTF-16 code units. Each invalid byte counts as 1.
- **State changes:** none.
- **Access:** [SemanticHighlighter](../syntax/semantic_highlighter.hpp.skel.md#class-semantichighlighter), for LSP positions when the server does not accept the `utf-8` position encoding.
- **Referred by:** [semantic_highlighter (implementation)](../syntax/semantic_highlighter.cpp.skel.md)


## function: text_columns

- **Inputs:** `text`: a short UTF-8 text, such as a label, a file name or a message.
- **Returns:** its display columns as the screen draws it: [display_width](#function-display_width) of each code point (a wide character two, a combining mark none), and one for each invalid byte. The one measure every view uses for labels, menus, prompts and the status line, so wide file names line up.
- **State changes:** none.
- **Access:** the views, the Markdown renderer.
- **Referred by:** [folder_tree_view](../ui/folder_tree_view.hpp.skel.md)
