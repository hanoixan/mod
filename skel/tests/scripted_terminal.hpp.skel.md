---
role: test
stamp: source c3a7e509, stand-in 5f1fa279
---
# module: scripted_terminal

A terminal for tests that run the real App: App's event loop drives it, keys come from a script, and what App writes is rendered into a screen the script can read. With it, App's glue (the flows between prompts, panels and documents) is tested without a pty.

- **Owns:** the emulated screen, the script and its results, a memory-sampling thread.
- **Access:** app_test, stress_test.
- **Required:** yes, for those tests.
- **Failure modes:** a step that never sees its screen within its limit ends the session as SIGTERM would (`terminated` from `wait`), and is reported as timed out.
- **Depends on:** [Terminal](../src/platform/terminal.hpp.skel.md#class-terminal)
- **Depends on:** [display_width](../src/text/utf8.hpp.skel.md#function-display_width)
- **Unknowns:** none

## class: VtScreen

The screen a terminal would show for what mod writes: cursor moves (`CSI row;col H`), clear (`CSI 2 J`), dim from SGR (`2` on, `0`/`22` off; `dim(r, c)`) and UTF-8 text, a wide character taking two cells and a zero-width one joining the cell before; attributes and modes are skipped, and OSC strings too, except that each title set (OSC 0) is recorded, in order, for `titles()`. Sequences cut across writes are completed by the next.

- **Inputs:** `rows`, `cols`.
- **State changes:** `feed(bytes)` updates the cells.
- **Owns:** one UTF-8 string per cell.
- **Access:** the scripts' conditions: `cell`, `dim`, `row`, `status` (the focused view's status line: the lowest row showing a `line:column` position, else the last row), `contains`, `titles`.

## function: anonymous_resident_bytes

- **Inputs:** none.
- **Returns:** the process's RssAnon from /proc/self/status, in bytes: heap and stacks, not mapped files.
- **State changes:** none.
- **Access:** the scripted terminal, stress_test.

## symbol: ScriptStep

A step: `name`, `keys` typed, `until` (a condition on the screen, or none: done once the keys are read) and `limit`; the run fills in `took`, `done` and `timed_out`. A step whose keys end in Esc waits its `esc_settle` first (400 ms by default, well over App's 250 ms between quitting menu keys, with room for a late timer on a slow machine, so steps never add up to a quit), since a lone Esc is only Esc once the decoder's wait has passed.

- **Access:** app_test, stress_test.

## class: ScriptedTerminal

- **Inputs:** `rows`, `cols`, the steps, `limit_scale` (multiplies every limit; the tests pass [time_budget](./time_budget.hpp.skel.md#function-time_budget)(1)).
- **State changes:** `wait` hands out the current step's keys, then checks its condition against the screen App drew before waiting; once met, the next step starts. While a condition is unmet it sleeps up to 20 ms or until `wake`. After the last step it returns `terminated`, so App ends in order. A thread samples anonymous memory every 20 ms (`peak_anonymous_bytes`), since a long save never waits.
- **Owns:** as the module.
- **Access:** app_test, stress_test.
- **Referred by:** [app_test](./app_test.cpp.skel.md)
- **Referred by:** [stress_test](./stress_test.cpp.skel.md)
