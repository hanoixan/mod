---
role: product
stamp: source ddb470b4, stand-in 1c7f1494
---
# module: merge_colors

The colors of diff3 conflict blocks, for the `auto_color_merges` [setting](../app/settings.hpp.skel.md#function-setting_specs): what [EditorView](../ui/editor_view.hpp.skel.md#function-set_merge_colors) draws over the highlighter's spans while a document holds blocks like those the disk merge writes (or git's, whatever their labels). A block is colored only while it is whole, so one being edited away goes back to plain text at once. Pure: it knows nothing of documents, views or themes, only text and styles.

- **Owns:** nothing.
- **Access:** public, free functions. Any thread.
- **Required:** optional — without it conflict blocks are plain text.
- **Failure modes:** listed on [merge_color_spans](#function-merge_color_spans).
- **Depends on:** [StyleSpan](./highlight.hpp.skel.md#symbol-stylespan)
- **Depends on:** [Style](./highlight.hpp.skel.md#symbol-style)
- **Unknowns:** none

## symbol: kMergeDiffMaxBytes

`inline constexpr std::size_t`, 32 KiB: the largest section whose characters are compared with the base. A larger one has its differing middle (what lies between the common start and the common end) colored whole, so the comparison never holds up a keystroke.

- **Access:** public; tests use it to build a section past it.
- **Referred by:** [merge_colors_test](../../tests/merge_colors_test.cpp.skel.md)

## function: merge_color_spans

A block is four marker lines in order, each seven of its character at a line's start followed by a space, a CR or the line's end: `<<<<<<<` (yours), `|||||||` (the common base), `=======` (theirs), `>>>>>>>`; whatever follows the marker is a free label. The sides are known by their place, first yours and last theirs, as git and mod's own merge write them. A `<<<<<<<` always starts a new block, even inside one being read; any other marker out of order ends the block being read without coloring it.

- **Inputs:** `text`: a `std::string_view` of the document's bytes; `base`: the document offset of its first byte.
- **Returns:** `std::vector<StyleSpan>`, document offsets, sorted and not overlapping, modifiers 0: each marker line, its line ending left out, in `Style::merge_banner`; yours' characters that the base has not in `Style::merge_your_insertion`, theirs' in `Style::merge_their_insertion`; the base's characters that yours alone deleted or replaced in `Style::merge_your_deletion`, theirs alone `Style::merge_their_deletion`, and both `Style::merge_both_deletion`; an insertion both sides made (the same characters, kept as each other's when yours is compared with theirs) in `Style::merge_both_insertion`, in each side; nothing else. Characters are code points, a byte that is not UTF-8 counting as one, compared with [diff_lines](../edit/merge3.hpp.skel.md#function-diff_lines) after the common start and end are cut off. The same text inserted in different places is each side's own. Empty when the text holds no whole block.
- **State changes:** none.
- **Access:** EditorView, for the window around the lines drawn; tests.
- **Failure modes:**
  - A block with a marker missing (no base, as in a two-way conflict; no end; no start), out of order, of eight characters, without a space after it or not at the line's start: no spans for it.
  - A section over `kMergeDiffMaxBytes` from its first to its last differing character, or one side's differing part empty: that part is colored whole, without a character diff.
  - `diff_lines` gives no edit list: the differing middle is colored whole, as above.
- **Referred by:** [merge_colors (implementation)](./merge_colors.cpp.skel.md)
- **Referred by:** [EditorView.set_merge_colors](../ui/editor_view.hpp.skel.md#function-set_merge_colors)
- **Referred by:** [merge_colors_test](../../tests/merge_colors_test.cpp.skel.md)
