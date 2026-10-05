---
role: test
stamp: source 3171cee3, stand-in 4df50a03
---
# module: input_test

Byte-sequence tables for every keymap binding as sent by xterm, rxvt, linux console, macOS Terminal and Windows Terminal. Split reads at every byte position; lone Esc versus Alt+key with `timeout`; bracketed paste containing ESC bytes; invalid UTF-8. A paste that never ends is ended by the idle timeout without swallowing later keys; a huge paste arrives in bounded pieces with nothing lost; OSC and DCS replies are skipped (split, unterminated), Alt+] and Alt+Shift+P still work; CSI u control code points are not typed. A reply longer than the limit is dropped whole however it arrives (BEL or ESC \\ ending it, ESC \\ split across reads, or a silence); a paste is never cut between CR and LF even when a chunk ends exactly at the CR; a paste's pieces carry `more` but the last, which may be empty.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** flaky timing-based tests are forbidden. Inject clocks, and use `EventQueue.drain` to run worker results deterministically.
- **Depends on:** [InputDecoder](../src/ui/input.hpp.skel.md#class-inputdecoder)
- **Depends on:** [Keymap](../src/app/keymap.hpp.skel.md#class-keymap)
- **Unknowns:** none. Tests use doctest; see [tests/CMakeLists.txt](./CMakeLists.txt.skel.md).
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
