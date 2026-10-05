---
role: test
stamp: source b3cd3d8d, stand-in a4bee3a2
---
# module: terminal_output_test

The xterm output: its enter, leave and frame sequences and SGR for every flag and color, byte for byte as earlier versions wrote them. The vt100 output: no color codes, no dim, italic or strike, no `?1049`, `?2004`, `?25` or `?2026` anywhere, bold, underline and reverse kept, and leave clears the screen. Detection from the environment: every listed TERM family, vt names winning over COLORTERM, each of COLORTERM, TERM_PROGRAM and WT_SESSION, and an empty environment undecided. Device attribute replies: VT100-class and VT220-class replies, a reply among other bytes found with its range, partial and malformed replies undecided. A Screen flushed through each output writes only that output's sequences. Cursor shapes: each style's xterm code, none in vt100, xterm's leave resetting the shape, and a Screen sending the shape once, again on a change and after a full redraw. Escape injection: OSC, C1 CSI/OSC, DEL and BEL in printed or put text never reach the terminal, and bogus widths do not shift the grid. A wide character put over another's second half in the last column leaves no half glyph. Raw 8-bit C1 bytes put or printed become `?`, and every cell holds valid UTF-8. The title is OSC 0 in xterm mode and nothing on a VT100, with controls, DEL, C1 and bytes that are not UTF-8 turned to `?`; xterm's enter and leave push and pop the terminal's title. add_flags dims an area already drawn, leaving its text and colors, clipped at the screen's edges. In vt100 mode box lines go through the line-drawing set (selected once for a run of them, deselected before other text and at the end of every frame and on leave) and the UI's symbols get same-width ASCII stand-ins; xterm sends every character as it is. A vt100 Screen draws a label's `…` as three dots, counted as three columns, and sends `─` inside the line-drawing set.

- **Owns:** test fixtures only.
- **Access:** run by CTest.
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON.
- **Failure modes:** none.
- **Depends on:** [TerminalOutput](../src/platform/terminal_output.hpp.skel.md#class-terminaloutput)
- **Depends on:** [mode_from_environment](../src/platform/terminal_output.hpp.skel.md#function-mode_from_environment)
- **Depends on:** [mode_from_device_attributes](../src/platform/terminal_output.hpp.skel.md#function-mode_from_device_attributes)
- **Depends on:** [Screen](../src/ui/screen.hpp.skel.md#class-screen)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
