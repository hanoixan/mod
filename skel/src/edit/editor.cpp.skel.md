---
role: product
unit: ./editor.hpp.skel.md
stamp: source a0ba00d4, stand-in 1149421c
---
# module: editor (implementation)

Implements [Editor](./editor.hpp.skel.md#class-editor). Horizontal motion and deletion read a small window around the cursor with `PieceTree.read` (copying into a stack buffer, because a cluster can span pieces) and call the grapheme-boundary functions, widening the window on `need_more` up to the cluster cap of [MAX_CLUSTER_CODE_POINTS](../text/utf8.hpp.skel.md#symbol-max_cluster_code_points) (32 code points). Vertical motion: find the current line's start with `find_lf_backward`, compute the cursor's display column with `display_width`, then move to the target line and walk until the sticky column is reached or exceeded. Each walk is bounded by the line's length. For very long lines, a cached `(line_start, column) → offset` checkpoint every 4 KiB avoids quadratic behavior.

- **Owns:** the column checkpoint cache, which is invalidated through `after_change`.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a pathological single-line file of many GB makes the first column computation O(line length). This is accepted.
- **Depends on:** [Editor](./editor.hpp.skel.md#class-editor)
- **Depends on:** [display_width](../text/utf8.hpp.skel.md#function-display_width)
- **Depends on:** [MAX_CLUSTER_CODE_POINTS](../text/utf8.hpp.skel.md#symbol-max_cluster_code_points)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
