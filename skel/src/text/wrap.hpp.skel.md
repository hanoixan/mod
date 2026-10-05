---
role: product
stamp: source db914a09, stand-in 26d77188
---
# module: wrap

The soft-wrap layout: how a line is cut into screen **rows** when View > Word Wrap is on. One module computes it for both users, so the rows [EditorView](../ui/editor_view.hpp.skel.md#class-editorview) draws are exactly the rows [Editor](../edit/editor.hpp.skel.md#class-editor) moves the cursor through.

**Where a row breaks.** A row takes as many grapheme clusters as fit in `cols` columns (cluster widths from [display_width](./utf8.hpp.skel.md#function-display_width); tab stops are counted from the start of the row). When the next cluster does not fit:

- if it is whitespace, that whitespace and any that follows **hangs** at the end of the row, beyond the width, so a row never starts with the space that separated two words;
- otherwise the row is cut back to just after the last whitespace on it (word wrap), or, when the row has no whitespace (one long word), exactly where it filled up.

A row always takes at least one cluster, so a width of 1 and a wide character still make progress.

**Files of any size.** A single line can be gigabytes long, and laying out a line from its start would cost its whole length. A line is therefore laid out in independent **blocks** of `kWrapBlock` (4096) bytes: block *k* starts at the first byte at or after `line_start + k × 4096` that does not continue a UTF-8 sequence, and a row never spans two blocks. Finding the row that contains a position, the next row or the previous row reads at most one block, however long the line is, and nothing is cached or invalidated. The price is that one row per 4 KiB of a very long line may be cut short at the block's end, and a grapheme cluster of several code points that straddles a block boundary is split between two rows (the cursor still never stops inside it). Lines shorter than a block, which is nearly all lines, are unaffected.

A row is named by the byte offset of its first byte. A position where one row ends and the next begins belongs to the next row; the end of the line belongs to the line's last row.

- **Owns:** nothing. The layout is a pure function of the text, the width and the tab width.
- **Access:** public. Main thread.
- **Required:** optional — only while word wrap is on.
- **Failure modes:** none; costs are bounded as described above. Finding a line's start still walks back to the previous line feed through [PieceTree.find_lf_backward](./piece_tree.hpp.skel.md#function-find_lf_backward), as everything else does.
- **Depends on:** [PieceTree](./piece_tree.hpp.skel.md#class-piecetree)
- **Depends on:** [next_grapheme_boundary](./utf8.hpp.skel.md#function-next_grapheme_boundary)
- **Depends on:** [display_width](./utf8.hpp.skel.md#function-display_width)
- **Depends on:** [char_class](./utf8.hpp.skel.md#function-char_class)
- **Unknowns:** none

## symbol: kWrapBlock

`inline constexpr std::uint64_t kWrapBlock = 4096;`: the block size of the layout, in bytes; see the module.

- **Access:** public.

## function: wrap_row_length

- **Inputs:** `text`: the bytes from a row's start to the end of its block or line, with no line break; `cols`: the columns a row may fill (less than 1 counts as 1); `tab_width`.
- **Returns:** the byte length of the first row, by the rules above; 0 only for empty text.
- **State changes:** none.
- **Access:** WrapLayout and tests.

## class: WrapLayout

- **Inputs:** a `const PieceTree&`; `cols`; `tab_width`. Cheap to construct: it holds only those.
- **State changes:** none.
- **Owns:** nothing.
- **Access:** EditorView (drawing and scrolling) and Editor (Up, Down, PageUp, PageDown).
- **Referred by:** [editor_view](../ui/editor_view.hpp.skel.md)
- **Referred by:** [editor](../edit/editor.hpp.skel.md)
- **Referred by:** [wrap (implementation)](./wrap.cpp.skel.md)
- **Referred by:** [wrap_test](../../tests/wrap_test.cpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)

### function: row_start

- **Inputs:** `pos`: a byte offset.
- **Returns:** the start of the row containing `pos`.
- **State changes:** none. Reads at most one block.
- **Access:** as the class.

### function: row_end

- **Inputs:** the start of a row.
- **Returns:** the end of that row: the start of the next row of the same line, or the line's end (before its LF or CR LF).
- **State changes:** none.
- **Access:** as the class.

### function: next_row

- **Inputs:** the start of a row.
- **Returns:** the start of the row after it, which is the first row of the next line after a line's last row; `npos` at the last row of the text. A text that ends with a line feed has a last, empty row after it.
- **State changes:** none.
- **Access:** as the class.

### function: prev_row

- **Inputs:** the start of a row.
- **Returns:** the start of the row before it, which is the last row of the previous line before a line's first row; `npos` at the first row of the text.
- **State changes:** none. Reads at most one block.
- **Access:** as the class.

### function: line_start

- **Inputs:** `pos`: a byte offset.
- **Returns:** the start of the line containing `pos`: the offset after the previous line feed, or 0.
- **State changes:** none.
- **Access:** as the class.
- **Depends on:** [PieceTree.find_lf_backward](./piece_tree.hpp.skel.md#function-find_lf_backward)

### function: line_end

- **Inputs:** the start of a line.
- **Returns:** the end of that line's text: the offset of its line feed, or of the CR before it for a CR LF ending, or the end of the text for a last line with no line feed.
- **State changes:** none.
- **Access:** as the class.
- **Depends on:** [PieceTree.find_lf_forward](./piece_tree.hpp.skel.md#function-find_lf_forward)
