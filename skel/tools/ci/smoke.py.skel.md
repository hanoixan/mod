---
role: product
untested: a CI script; every CI and release run exercises it on each platform
---
# module: smoke.py

The smoke test of a built mod, for every platform: `python3 tools/ci/smoke.py <mod>`. In a temporary folder it writes `smoke.txt` holding `hello\n`, starts mod on it in a pseudo-terminal (24×80, `TERM=xterm-256color`), waits up to 20 s for `hello` in its output, types `X`, Ctrl+S and Ctrl+Q, and waits for mod to exit. It passes (exit 0, one line) when mod exited with 0 and the file holds `Xhello\n`; otherwise it exits non-zero saying what was missing (with the end of mod's output on a timeout).

On Windows it runs `mod.exe` in a Windows pseudo-console (ConPTY) through `pywinpty`, the way Windows Terminal runs it, so the MSYS2 runtime's console path is what is tested; elsewhere it uses Python's `pty`.

- **Owns:** a temporary folder.
- **Access:** CI ([package.sh](./package.sh.skel.md)'s workflows), by hand.
- **Required:** always for CI.
- **Failure modes:** mod never draws the file, never exits, exits non-zero, or did not save: each fails with a message.
- **Depends on:** none
- **Referred by:** none known (the workflows in `.github/workflows`, and developers)
- **Unknowns:** none
