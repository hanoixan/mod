---
role: product
unit: ./terminal.hpp.skel.md
stamp: source 99268116, stand-in 63e054f9
---
# module: terminal_posix

The POSIX backend for [Terminal](./terminal.hpp.skel.md#class-terminal), using termios, `poll`, and a self-pipe for wake and SIGWINCH.

- **Owns:** the saved `struct termios`, the self-pipe file descriptors, and the SIGWINCH handler installation.
- **Access:** internal. Constructed only by `make_terminal`.
- **Required:** always. It is the only terminal backend, including in the MSYS2 MSYS environment.
- **Depends on:** [Terminal](./terminal.hpp.skel.md#class-terminal)
- **Failure modes:** each of these defeats a required key binding unless handled in `enter_raw_mode`:
  - `IXON` left on: Ctrl-S freezes output and Ctrl-Q resumes. Clear `IXON`.
  - `ISIG` left on: Ctrl-C sends SIGINT, Ctrl-Z sends SIGTSTP, and Ctrl-\\ sends SIGQUIT. Clear `ISIG`.
  - `IEXTEN` left on: Ctrl-V is swallowed as literal-next, and on macOS Ctrl-O as discard. Clear `IEXTEN`.
  - On macOS and BSD, `VDSUSP` makes Ctrl-Y a delayed suspend. Set `c_cc[VDSUSP] = _POSIX_VDISABLE` where it is defined.
  - `ICRNL` left on: Enter and Ctrl-J become indistinguishable, and Ctrl-M arrives as LF. Clear `ICRNL`.
  - Also clear `ECHO`, `ICANON`, `OPOST` (the screen writes `\r\n` itself), `INPCK`, `ISTRIP` and `BRKINT`. Set `CS8`, `VMIN=0`, `VTIME=0`.
  - `tcsetattr` fails, for example on a non-TTY: return `ErrorCode::unsupported`.
  - A killed parent leaves the terminal in raw mode. Mitigated by signal-handler restore; SIGKILL cannot be handled. Document `reset` in the README.
- **Unknowns:** none
- **Referred by:** [CMakeLists.txt](../../CMakeLists.txt.skel.md)

## class: PosixTerminal

- **Inputs:** stdin fd 0 and stdout fd 1. Both must satisfy `isatty`.
- **State changes:** wake latch = one byte in a non-blocking self-pipe (`pipe` + `O_NONBLOCK` + `FD_CLOEXEC`). The SIGWINCH handler writes `'R'` to the same pipe. `wait` polls `{stdin, pipe}` and drains the pipe to tell `woken` and `resized` apart.
- **Owns:** the pipe fds, which are closed in the destructor, and the saved termios.
- **Access:** through `Terminal`.
- **Depends on:** [termios(3) / poll(2)](https://pubs.opengroup.org/onlinepubs/9699919799/basedefs/termios.h.html)
