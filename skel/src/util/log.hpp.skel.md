---
role: product
untested: no test has been written for it yet; logging is off unless MOD_LOG is set
stamp: source fbaafb5a, stand-in 934b6dc9
---
# module: log

Opt-in diagnostic logging. Nothing is ever written to the terminal, because that would corrupt the screen.

- **Owns:** the declarations only. The state lives in [log.cpp](./log.cpp.skel.md).
- **Access:** public free functions, callable from any thread.
- **Required:** optional — without it there are no diagnostics.
- **Failure modes:** the log file cannot be opened, so logging silently stays off.
- **Depends on:** none
- **Unknowns:** none

## symbol: LogLevel

`enum class LogLevel { debug, info, warn, error };`

- **Access:** public.

## function: init_logging

- **Inputs:** ambient: the environment variable `MOD_LOG`, a path. `MOD_LOG_LEVEL` is optional (`debug|info|warn|error`, default `info`).
- **Returns:** nothing.
- **State changes:** opens the file for appending if `MOD_LOG` is set.
- **Access:** called once by [main](../main.cpp.skel.md#function-main) before anything else.
- **Referred by:** [main](../main.cpp.skel.md)

## function: log

- **Inputs:** `level`; `fmt` and `args` as for `std::format`.
- **Returns:** nothing.
- **State changes:** appends one line, `ISO-8601 time, thread id, level, message`, if `level` is at or above the threshold and logging is on.
- **Access:** any thread. Writes are serialized internally.
- **Referred by:** [log (implementation)](./log.cpp.skel.md)
- **Referred by:** [line_scanner (implementation)](../text/line_scanner.cpp.skel.md)
