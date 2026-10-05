---
role: product
stamp: source cd2a7daf, stand-in 2d821882
---
# module: terminal

An abstract terminal device: raw mode, byte input, byte output, size, and the main loop's blocking wait with a cross-thread wake. All escape-sequence *meaning* lives in [screen](../ui/screen.hpp.skel.md) and [input](../ui/input.hpp.skel.md). This layer only moves bytes.

- **Owns:** the `Terminal` interface and the factory that picks the platform backend.
- **Access:** public. [App](../app/app.hpp.skel.md#class-app) owns exactly one instance, created by `make_terminal`.
- **Required:** always.
- **Failure modes:** stdin or stdout is not a TTY, in which case `make_terminal` returns `ErrorCode::unsupported` and `main` exits with a message. A terminal size of 0×0 (some CI PTYs) is treated as 80×24.
- **Depends on:** [Result](../util/error.hpp.skel.md#symbol-result)
- **Depends on:** [TerminalOutput](./terminal_output.hpp.skel.md#class-terminaloutput)
- **Unknowns:** none

## symbol: TerminalSize

`{ int rows; int cols; }`, both ≥ 1.

- **Access:** public.

## class: Terminal

A pure-virtual interface.

- **Inputs:** none (abstract).
- **State changes:** invariant: between `enter_raw_mode` and `restore`, the terminal is in raw mode, and after `start_screen` it is in the output's screen mode (for xterm, the alternate screen with bracketed paste on). After `restore`, the original termios or console modes are back exactly.
- **Owns:** the saved original terminal modes.
- **Access:** main thread, except `wake`.
- **Referred by:** [app](../app/app.hpp.skel.md)
- **Referred by:** [terminal_posix](./terminal_posix.cpp.skel.md)
- **Referred by:** [editor_view_test](../../tests/editor_view_test.cpp.skel.md)
- **Referred by:** [keymap_view_test](../../tests/keymap_view_test.cpp.skel.md)
- **Referred by:** [menu_test](../../tests/menu_test.cpp.skel.md)
- **Referred by:** [settings_view_test](../../tests/settings_view_test.cpp.skel.md)
- **Referred by:** [doc_search_test](../../tests/doc_search_test.cpp.skel.md)
- **Referred by:** [colors_view_test](../../tests/colors_view_test.cpp.skel.md)
- **Referred by:** [file_dialog_test](../../tests/file_dialog_test.cpp.skel.md)
- **Referred by:** [text_field_test](../../tests/text_field_test.cpp.skel.md)
- **Referred by:** [scripted_terminal](../../tests/scripted_terminal.hpp.skel.md)

### function: enter_raw_mode

- **Inputs:** none.
- **Returns:** `Status`.
- **State changes:** saves the current modes and switches to raw input that delivers every key listed in the keymap: no echo, no canonical mode, no signal keys, no flow control, no literal-next. Writes no escape sequences: the screen is taken over by `start_screen`, after the terminal mode is known. Installs handlers so that `restore` runs on SIGINT, SIGQUIT (which only another process can send, the signal keys being off), SIGSEGV, SIGABRT and SIGBUS before the signal is re-raised; handlers that make `wait` report SIGTERM and SIGHUP as `terminated`, so the application ends in order and every history reaches its sidecar; and a SIGTSTP handler that runs [suspend](#function-suspend)'s stop path. The terminal's signal keys stay off, so Ctrl+C reaches the application as Copy and Ctrl+Z as Undo; the application stops itself with `suspend`.
- **Access:** once, from `App` startup.

### function: start_screen

- **Inputs:** the [TerminalOutput](./terminal_output.hpp.skel.md#class-terminaloutput) for the session.
- **Returns:** nothing.
- **State changes:** writes its `enter()` and remembers it: `restore` writes its `leave()`, and a continued suspended job writes `enter()` again.
- **Access:** once, from `App` startup, after detection.

### function: restore

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** writes `ESC[?2004l ESC[?1049l ESC[?25h ESC[0m` and restores the saved modes. It is idempotent and async-signal-safe: no allocation, only `write` and `tcsetattr`.
- **Access:** `App` shutdown, the signal handlers, and the top-level exception handler in `main`.

### function: size

- **Inputs:** none.
- **Returns:** the current `TerminalSize`.
- **State changes:** none.
- **Access:** main thread, after a resize event and at startup.

### function: read_input

- **Inputs:** `buf`: a writable byte span.
- **Returns:** the number of bytes read (0 if none are available), or an error. Never blocks.
- **State changes:** consumes bytes from the input device.
- **Access:** main thread, after `wait` reports input.

### function: write

- **Inputs:** `bytes`.
- **Returns:** `Status`. It loops until every byte is written and retries on `EINTR`/`EAGAIN`.
- **State changes:** writes to the output device.
- **Access:** main thread. [Screen.flush](../ui/screen.hpp.skel.md#function-flush) batches a whole frame into one call.
- **Referred by:** [screen](../ui/screen.hpp.skel.md)

### function: wait

- **Inputs:** `timeout_ms`: −1 means forever. [App](../app/app.hpp.skel.md#function-run) passes a finite value while [InputDecoder](../ui/input.hpp.skel.md#class-inputdecoder) holds a pending lone ESC.
- **Returns:** a bit set (`WaitEvents`, a `uint8_t`) of the `WaitEvent` bits `input_ready`, `woken`, `resized`, `timed_out` and `terminated` (SIGTERM or SIGHUP arrived, or the terminal can no longer be waited on: the application ends in order).
- **State changes:** clears the wake and resize latches it reports.
- **Access:** main thread only, from the event loop.

### function: wake

- **Inputs:** none.
- **Returns:** nothing.
- **State changes:** sets the wake latch so that a concurrent or later `wait` returns promptly. Multiple wakes collapse into one.
- **Access:** any thread. Async-signal-safe on POSIX because it is used by the SIGWINCH handler path.
- **Referred by:** [EventQueue](../util/event_queue.hpp.skel.md#class-eventqueue)

### function: suspend

- **Inputs:** none.
- **Returns:** nothing, once the process has been continued.
- **State changes:** stops the process as a shell job. On POSIX it raises SIGTSTP, whose handler (also reached by a SIGTSTP sent from outside) is async-signal-safe: it restores the terminal as `restore` does, stops the process with the default action, and when the job is continued (`fg`, SIGCONT) saves the shell's current modes, re-applies raw mode, writes the enter sequence again, and sets the resize latch so the next `wait` reports `resized` and the caller redraws everything. A process that cannot be stopped (an orphaned process group) continues at once. The base-class default does nothing, for terminals in tests.
- **Access:** App (the `Suspend` command).

### function: can_suspend

- **Inputs:** none.
- **Returns:** whether `suspend` could come back: true everywhere except on Windows when no MSYS2 or Cygwin process started mod (its parent then shows as pid 1, as when started from PowerShell or Windows Terminal), where a stopped mod could never be continued. The base-class default is true.
- **State changes:** none.
- **Access:** App, before `suspend`.

## function: make_terminal

- **Inputs:** none. It reads the process's stdin and stdout.
- **Returns:** `Result<std::unique_ptr<Terminal>>`: a [PosixTerminal](./terminal_posix.cpp.skel.md#class-posixterminal), the only backend.
- **State changes:** none. Raw mode is entered separately.
- **Access:** [App](../app/app.hpp.skel.md#class-app) construction.
- **Referred by:** [app (implementation)](../app/app.cpp.skel.md)
