---
role: product
unit: ./merge3.hpp.skel.md
stamp: source a5685de5, stand-in 09434f6c
---
# module: merge3 (implementation)

Implements [merge3](./merge3.hpp.skel.md). The diff trims the common prefix and suffix, then splits on the middle snake and recurses on each side. The merge walks both edit lists in base order, growing a chunk while an edit of either side overlaps it or touches it where one of them inserts, and maps the chunk's base range into ours and theirs by the growth of the edits before it.

- **Owns:** nothing.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** none beyond the header's.
- **Depends on:** [merge3](./merge3.hpp.skel.md#function-merge3)
- **Depends on:** [diff_lines](./merge3.hpp.skel.md#function-diff_lines)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
