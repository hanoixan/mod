#!/usr/bin/env python3
"""Smoke test of a built mod: start it in a pseudo-terminal on a file, type, save, quit.

    python3 tools/ci/smoke.py path/to/mod

On Windows it runs mod.exe in a Windows pseudo-console (ConPTY, through pywinpty: pip
install pywinpty), the way Windows Terminal runs it; elsewhere in a POSIX pty. Exits 0
when the file holds the edit and mod quit with status 0.
"""
import os
import sys
import tempfile
import time
from pathlib import Path

TIMEOUT = 20.0


def wait_for(read, want, what):
    """Reads output until `want` (bytes) is in it."""
    seen = b""
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        seen += read()
        if want in seen:
            return seen
        time.sleep(0.05)
    sys.exit(f"smoke: timed out waiting for {what}; last output: {seen[-400:]!r}")


def run_posix(mod, path):
    import pty
    import select

    pid, fd = pty.fork()
    if pid == 0:
        os.environ["TERM"] = "xterm-256color"
        os.execv(mod, [mod, str(path)])

    def read():
        r, _, _ = select.select([fd], [], [], 0.1)
        if not r:
            return b""
        try:
            return os.read(fd, 65536)
        except OSError:
            return b""

    def write(b):
        os.write(fd, b)

    wait_for(read, b"hello", "the file's text")
    write(b"X")
    time.sleep(0.3)
    write(b"\x13")  # Ctrl+S
    time.sleep(0.5)
    write(b"\x11")  # Ctrl+Q
    deadline = time.time() + TIMEOUT
    while time.time() < deadline:
        read()
        done, status = os.waitpid(pid, os.WNOHANG)
        if done:
            return os.waitstatus_to_exitcode(status)
    os.kill(pid, 9)
    sys.exit("smoke: mod did not quit")


def run_windows(mod, path):
    from winpty import PtyProcess

    proc = PtyProcess.spawn([mod, str(path)], dimensions=(24, 80))

    def read():
        try:
            return proc.read(65536).encode("utf-8", "replace")
        except EOFError:
            return b""

    wait_for(read, b"hello", "the file's text")
    proc.write("X")
    time.sleep(0.3)
    proc.write("\x13")
    time.sleep(0.5)
    proc.write("\x11")
    deadline = time.time() + TIMEOUT
    while time.time() < deadline and proc.isalive():
        read()
        time.sleep(0.05)
    if proc.isalive():
        proc.terminate(force=True)
        sys.exit("smoke: mod did not quit")
    return proc.exitstatus


def main():
    if len(sys.argv) != 2:
        sys.exit(__doc__)
    mod = os.path.abspath(sys.argv[1])
    with tempfile.TemporaryDirectory() as tmp:
        path = Path(tmp) / "smoke.txt"
        path.write_bytes(b"hello\n")
        status = run_windows(mod, path) if os.name == "nt" else run_posix(mod, path)
        text = path.read_bytes()
        if status != 0:
            sys.exit(f"smoke: mod exited with {status}")
        if text != b"Xhello\n":
            sys.exit(f"smoke: the file holds {text!r}, not the edit")
    print("smoke: started, edited, saved and quit")


if __name__ == "__main__":
    main()
