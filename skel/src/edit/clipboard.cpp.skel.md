---
role: product
unit: ./clipboard.hpp.skel.md
stamp: source e8ee2558, stand-in 04fe1099
---
# module: clipboard (implementation)

Implements [Clipboard](./clipboard.hpp.skel.md#class-clipboard), including a small base64 encoder for OSC 52.

- **Owns:** nothing extra.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a paste whose `PieceRun` refers to buffers from a *different* document (after File > Open). Paste must register the foreign buffers into the new tree's table, or materialize them, before calling `insert_run`. Editor materializes: it copies the bytes from the clipboard's views into the new document's add buffer and pastes that run.
- **Depends on:** [Clipboard](./clipboard.hpp.skel.md#class-clipboard)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
