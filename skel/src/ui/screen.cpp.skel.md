---
role: product
unit: ./screen.hpp.skel.md
stamp: source a2e34043, stand-in 8deefea7
---
# module: screen (implementation)

Implements [Screen](./screen.hpp.skel.md#class-screen). The SGR encoding uses a full reset (`ESC[0m`) and then sets only the flags needed when attributes change. Foreground colors 0–7 use codes 30–37 and 8–15 use 90–97; background colors use 40–47 and 100–107; the default color uses 39 or 49.

- **Owns:** the encoder.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** output buffer growth on full repaints of huge terminals. The buffer is reused between frames.
- **Depends on:** [Screen](./screen.hpp.skel.md#class-screen)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
