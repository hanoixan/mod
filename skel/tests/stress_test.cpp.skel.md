---
role: test
stamp: source c4659bd8, stand-in 02c7809e
---
# module: stress_test

Gigabyte files through the real editor, App on a [ScriptedTerminal](./scripted_terminal.hpp.skel.md#class-scriptedterminal), labelled `stress` so the ordinary run leaves it out; `ctest --preset linux-release-stress` runs it, as does CI's stress job. Files go to `MOD_STRESS_DIR` (else the scratch directory) and are removed however a case ends; `MOD_STRESS_BYTES` replaces every size, for a quick run while working on it.

- **A 1 GB and a 4 GB text file** of numbered ~70-byte lines: open until the line count is known, Ctrl+End to the last line, Ctrl+G to the middle line (shown), type there, Ctrl+F to a line near the end, save, quit; on disk the file is the original with the typed text at the middle line and nothing else, byte for byte.
- **A 1 GB binary file** of random bytes from a fixed seed with a marker planted three quarters in: open, twenty pages down and five up, Ctrl+End, Ctrl+Home, Ctrl+F to the marker's line, a byte typed and taken back, save, quit; on disk the file is unchanged, byte for byte, and no cell ever drawn held anything but valid UTF-8 without controls.
- **Limits** are performance checks: about ten times what a desktop with an SSD takes (open 30 s, search 10 s and save 70 s a GB, at least one GB's worth), scaled by `MOD_TEST_TIME_SCALE`; and a session may add at most 256 MiB of anonymous memory, whatever the size (the file is mapped).

- **Owns:** test fixtures only.
- **Access:** run by CTest (label `stress`).
- **Required:** conditional — when `MOD_BUILD_TESTS` is ON; run on request.
- **Failure modes:** none.
- **Depends on:** [App](../src/app/app.hpp.skel.md#class-app)
- **Depends on:** [ScriptedTerminal](./scripted_terminal.hpp.skel.md#class-scriptedterminal)
- **Depends on:** [time_budget](./time_budget.hpp.skel.md#function-time_budget)
- **Unknowns:** none
- **Referred by:** [tests/CMakeLists.txt](./CMakeLists.txt.skel.md)
