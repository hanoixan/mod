---
role: product
unit: ./input.hpp.skel.md
stamp: source ed45b57c, stand-in a7af65ca
---
# module: input (implementation)

Implements [InputDecoder](./input.hpp.skel.md#class-inputdecoder). It uses table-driven decoding of CSI final bytes and `~` codes. The modifier parameter is `1 + bits`, where Shift=1, Alt=2 and Ctrl=4.

```text
ESC [ A/B/C/D          arrows           ESC [ 1 ; m A..D     arrows + mods
ESC [ H / F, ESC O H/F home/end         ESC [ 1 ; m H / F    home/end + mods (Ctrl=5)
ESC [ 1~ 7~ / 4~ 8~    home / end (rxvt, linux)
ESC [ 2~ 3~ 5~ 6~      ins del pgup pgdn  (; m for mods)
ESC O P..S, ESC [ 15~..24~   F1..F12
ESC [ 200~ ... ESC [ 201~    bracketed paste
ESC <byte>             Alt + key        0x01..0x1A  Ctrl+A..Z (except 09 Tab, 0D Enter)
0x7F / 0x08            Backspace / Ctrl+Backspace
ESC [ Z                Shift+Tab (BackTab)
ESC [ a..d, ESC O a..d  rxvt Shift+arrows, Ctrl+arrows
ESC [ n $ / ^ / @      rxvt Shift / Ctrl / Ctrl+Shift on the `~` keys (Home 7, End 8, …)
ESC [ 13;m u, ESC [ 27;m;13 ~   Enter + mods (terminals that report modified Enter)
```

- **Owns:** the tables.
- **Access:** internal.
- **Required:** always.
- **Failure modes:** a `CSI 201~` split across reads inside a paste. Match the terminator incrementally.
- **Depends on:** [InputDecoder](./input.hpp.skel.md#class-inputdecoder)
- **Depends on:** [decode](../text/utf8.hpp.skel.md#function-decode)
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)
