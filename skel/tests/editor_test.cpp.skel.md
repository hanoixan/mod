---
role: test
stamp: source 52eebab3, stand-in 49834078
---
# module: editor_test

Every `Motion` with and without Shift; Left, Right, Backspace and Delete over extended grapheme clusters (an emoji ZWJ family, a skin-tone modifier, a flag pair and an odd run of regional indicators, `e` plus combining acute, a Hangul syllable spelled as L V T jamo, a Devanagari conjunct for GB9c, CR LF, and invalid bytes); clusters that span pieces; the cluster cap on a long run of combining marks (a base plus 31 combining marks is one cluster; a base plus 32 splits after the 32nd code point, in both directions); the full UAX #29 conformance table in [grapheme_break_cases.inc](./grapheme_break_cases.inc.skel.md) run through both boundary functions; word boundaries with Unicode and punctuation; the sticky column across lines with wide characters and tabs; cursor never placed between CR and LF; with a wrap width, Up, Down and the page keys move by screen row, keep the column inside the row across a short row, never land on the next row's start, count rows for the page keys, leave Home and End whole-line, restart tab stops on each row, and move by line again once the width is removed; copy, cut and paste via `PieceRun`; text pasted from the terminal (`paste_text`) gets the document's line ending for a bare CR, a CR LF and an LF alike, in an LF document and in a CR LF document, as one `paste` node, also when it replaces a selection; copy and cut with no selection do nothing (clipboard unchanged, no undo node); typing over a selection is one undo node; Tab inserts one `\t`; the display width of each kind of unit (tab stops, `^X` controls, C1 controls as six-cell `\u0080` escapes, invalid bytes, wide and zero-width characters); display columns and the sticky column with tab widths 4 (default), 1 and 8, and after `set_tab_width`; OSC 52 is written for a copy of exactly 100 000 bytes and not for 100 001; after an in-place save (with no sidecar to rebind payloads), undo of a large deletion and paste of an older copy from the overwritten file still give the original bytes; content copied from another document pastes as a copy. Cut to line end: from mid-line as one `cut` node, the line break at the line end, a CR LF whole, nothing at the end of the text, the selection when there is one, consecutive presses appending one undo node each, a copy from another document breaking the append, and OSC 52 carrying the whole appended content. Indent with spaces to the next tab stop and with a tab; outdent of one line, of every selected line, a leading tab, and nothing left to remove. Two editors on one document, as two split views: an edit through one moves the other's cursor, and the first's cursor before the change stays. Outdent keeps a backward selection over the same text. An in-place save after an insertion at the start keeps the text, the file, undo and redo right. An edit that joins invalid bytes into one character leaves the cursor on its boundary. text_columns: wide characters two columns, combining marks none, an invalid byte one. An in-place save over a file that cannot be written fails with the open error and leaves the file and the dirty state as they were. A paste in several pieces is undone in one step; two pastes are two.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [Editor](../src/edit/editor.hpp.skel.md#class-editor)
- **Depends on:** [char_class](../src/text/utf8.hpp.skel.md#function-char_class)
- **Depends on:** [display_width](../src/text/utf8.hpp.skel.md#function-display_width)
- **Depends on:** [next_grapheme_boundary](../src/text/utf8.hpp.skel.md#function-next_grapheme_boundary)
- **Depends on:** [prev_grapheme_boundary](../src/text/utf8.hpp.skel.md#function-prev_grapheme_boundary)
- **Depends on:** [grapheme_break_cases.inc](./grapheme_break_cases.inc.skel.md)
- **Depends on:** [Clipboard](../src/edit/clipboard.hpp.skel.md#class-clipboard)
- **Depends on:** [Document](../src/edit/document.hpp.skel.md#class-document)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
