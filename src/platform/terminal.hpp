#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>

#include "platform/terminal_output.hpp"
#include "util/error.hpp"

namespace mod {

// Both ≥ 1. A reported 0×0 is replaced by 80×24.
struct TerminalSize {
    int rows = 24;
    int cols = 80;
};

// Bits returned by Terminal::wait.
enum WaitEvent : std::uint8_t {
    input_ready = 1u << 0,
    woken = 1u << 1,
    resized = 1u << 2,
    timed_out = 1u << 3,
    terminated = 1u << 4,  // SIGTERM or SIGHUP, or the terminal can no longer be waited on
};
using WaitEvents = std::uint8_t;

// Moves bytes to and from the terminal; escape-sequence meaning lives in ui/.
class Terminal {
public:
    virtual ~Terminal() = default;

    // Raw mode only; installs the restore-and-re-raise handlers for SIGINT, SIGQUIT,
    // SIGSEGV, SIGABRT and SIGBUS, and handlers that report SIGTERM and SIGHUP as
    // `terminated` from `wait`, so the editor can end in order. The screen is taken over later, by `start_screen`.
    virtual Status enter_raw_mode() = 0;
    // Writes the output's `enter()` and remembers it: `restore` writes its `leave()`, and a
    // continued suspended job its `enter()` again. Tests' terminals need not override it.
    virtual void start_screen(const TerminalOutput& /*output*/) {}
    // Idempotent and async-signal-safe.
    virtual void restore() noexcept = 0;
    virtual TerminalSize size() = 0;
    // Never blocks; 0 when nothing is available.
    virtual Result<std::size_t> read_input(std::span<std::byte> buf) = 0;
    virtual Status write(std::span<const std::byte> bytes) = 0;
    // `timeout_ms` −1 waits forever. Clears the wake and resize latches it reports.
    virtual WaitEvents wait(int timeout_ms) = 0;
    // Any thread; async-signal-safe.
    virtual void wake() noexcept = 0;
    // Stops the process as a shell job (Ctrl+T): the terminal is restored first and raw mode
    // re-entered when the job continues, which reports `resized` from the next `wait` so the
    // caller redraws. The default does nothing, for terminals that cannot stop (tests).
    virtual void suspend() noexcept {}
    // Whether `suspend` can come back: false on Windows when no MSYS2 or Cygwin shell started
    // mod (from PowerShell or Windows Terminal nothing could continue it).
    virtual bool can_suspend() const noexcept { return true; }
};

// The POSIX backend; `unsupported` when stdin or stdout is not a TTY.
Result<std::unique_ptr<Terminal>> make_terminal();

}  // namespace mod
