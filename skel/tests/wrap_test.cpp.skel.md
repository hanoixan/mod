---
role: test
stamp: source eaeca2b2, stand-in cf3524ec
---
# module: wrap_test

`wrap_row_length`: a row breaks after the last whitespace that fits; a text that fits exactly is one row; a word longer than the row breaks at the edge, after a shorter word has had its own row; whitespace that does not fit hangs at the end of its row; wide characters (two fit in five columns, one alone in one column), a combining mark stays with its base, tabs run to the next stop of the row and follow the tab width, control characters count two cells. `WrapLayout` over a short text: the rows of a wrapped line, an empty line and the empty row after a final line feed; which row a boundary position and the line end belong to; `next_row` and `prev_row` across lines and `npos` at both ends; an empty text is one empty row; CR LF ends a line before the CR; a width of one column (and of zero) still makes progress. A line longer than a block: the row before a block boundary is cut short at it, rows on both sides are found from any position, the line end belongs to the last row, and walking forward and backward visits the same rows, which tile the text. A block boundary never splits a multi-byte character.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [WrapLayout](../src/text/wrap.hpp.skel.md#class-wraplayout)
- **Depends on:** [wrap_row_length](../src/text/wrap.hpp.skel.md#function-wrap_row_length)
- **Depends on:** [PieceTree](../src/text/piece_tree.hpp.skel.md#class-piecetree)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
