---
role: product
stamp: source 8ffb7471, stand-in 61244a43
---
# module: merge3

The line diff and three-way merge behind Merge in place ([App.ask_disk_change](../app/app.hpp.skel.md#function-ask_disk_change)). Pure and thread-free: it works on line hashes, so a merge of a file of any size holds three arrays of 8-byte hashes and line starts, never three copies of the text, and can run on a worker thread.

- **Owns:** nothing.
- **Access:** public.
- **Required:** always.
- **Failure modes:** two different lines with the same 64-bit FNV-1a hash compare equal; accepted as vanishingly rare. A diff of two wholly different huge files is slow (Myers' O((N+M)·D)); `stop` ends it, and App's Esc requests that.
- **Depends on:** none
- **Unknowns:** none

## class: LineIndexer

- **Inputs:** bytes, fed in pieces of any size.
- **State changes:** builds a `LineIndex { hash, start }`: one hash per line, its line feed included, and the line starts with the text's end last (`start` one longer than `hash`). `finish` closes a last line without a line feed, whose hash is altered so it never equals the same line with one.
- **Owns:** the index until `finish` hands it over.
- **Access:** App, over the text as last saved, the text being edited, and the file on disk.
- **Referred by:** [App.ask_disk_change](../app/app.hpp.skel.md#function-ask_disk_change)
- **Referred by:** [merge3_test](../../tests/merge3_test.cpp.skel.md)

## function: diff_lines

- **Inputs:** `a`, `b`: line hashes; `stop`: ends it early; `progress`: when given, counts a's lines settled.
- **Returns:** `std::optional<std::vector<LineEdit>>`: the edits `{a0, a1, b0, b1}` (a's lines [a0, a1) become b's [b0, b1)), in order, adjacent ones joined, as few lines changed as can be (Myers' diff with the linear-space middle snake, after the common prefix and suffix); nullopt when stopped.
- **State changes:** none.
- **Access:** merge3, tests.
- **Referred by:** [merge3 (implementation)](./merge3.cpp.skel.md)
- **Referred by:** [merge3_test](../../tests/merge3_test.cpp.skel.md)

## function: merge3

- **Inputs:** `base`, `ours`, `theirs`: line hashes; `stop`; `progress` (base lines settled over both diffs, 2 × base's lines in all).
- **Returns:** `std::optional<std::vector<MergeHunk>>`, in order of `ours`: `take_theirs` (ours' lines [ours0, ours1) become theirs' [theirs0, theirs1): a change only theirs made) or `conflict` (both changed base lines [base0, base1), differently, or both inserted at one place); nullopt when stopped. A change of ours alone needs nothing (it is in the text); the same change on both sides is taken once; changes that only sit next to each other, both changing lines, are not a conflict.
- **State changes:** none.
- **Access:** App's merge worker, tests.
- **Referred by:** [App.ask_disk_change](../app/app.hpp.skel.md#function-ask_disk_change)
- **Referred by:** [merge3 (implementation)](./merge3.cpp.skel.md)
- **Referred by:** [merge3_test](../../tests/merge3_test.cpp.skel.md)
