---
role: product
untested: a generator run by hand when the look changes; its output is looked at
stamp: source 85e25a9f, stand-in 260891e0
---
# module: screenshot

Makes the README's pictures by running mod itself: Python 3 with `pyte` (a terminal emulator) and Pillow. It starts the built `mod` (default `build/linux-release/mod`) in a pseudo-terminal of 110×34 with a fresh configuration folder, sends keys, lets the screen settle, and draws pyte's screen into a PNG with DejaVu Sans Mono (bold for bold), xterm's 16 colors, on a dark background, with the cursor shown as a bar.

- [screenshot.png](../docs/images/screenshot.png.skel.md): mod's own source in two split views (`src/ui/reading_layout.cpp` above, `src/ui/reading_layout.hpp` below), syntax colored, line numbers on.
- [undo-history.png](../docs/images/undo-history.png.skel.md): a copy of a source file edited, an edit undone and a different one made, so the Undo History pane shows a branch.

The source files are copied into a temporary folder first, so the repository is never changed.

- **Owns:** a temporary folder.
- **Access:** developers.
- **Required:** optional.
- **Failure modes:** pyte, Pillow or the font missing: stops with a message saying which.
- **Depends on:** none
- **Unknowns:** none
- **Referred by:** [screenshot.png](../docs/images/screenshot.png.skel.md)
- **Referred by:** [undo-history.png](../docs/images/undo-history.png.skel.md)
