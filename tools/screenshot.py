#!/usr/bin/env python3
"""Makes the README's pictures by running mod in a pseudo-terminal and drawing its screen.

    python3 tools/screenshot.py [--mod build/linux-release/mod] [--out docs/images]

Needs pyte (a terminal emulator) and Pillow: pip install pyte pillow.
"""
import argparse
import fcntl
import json
import os
import pty
import select
import shutil
import struct
import sys
import tempfile
import termios
import time
from pathlib import Path

try:
    import pyte
    from PIL import Image, ImageDraw, ImageFont
except ImportError as e:
    sys.exit(f"screenshot.py: {e.name} is needed (pip install pyte pillow)")

ROOT = Path(__file__).resolve().parent.parent
ROWS, COLS = 34, 110
FONT_DIR = Path("/usr/share/fonts/truetype/dejavu")
FONT_SIZE = 15

ESC = b"\x1b"
SHIFT_DOWN = b"\x1b[1;2B"
UP, RIGHT = b"\x1b[A", b"\x1b[C"
CTRL_G, CTRL_Z = b"\x07", b"\x1a"

# xterm's 16 colors, and the page around them.
PALETTE = {
    "black": "#000000", "red": "#cd0000", "green": "#00cd00", "brown": "#cdcd00", "yellow": "#cdcd00",
    "blue": "#3b6fe0", "magenta": "#cd00cd", "cyan": "#00cdcd", "white": "#e5e5e5",
    "brightblack": "#7f7f7f", "brightred": "#ff5f5f", "brightgreen": "#5fff5f", "brightbrown": "#ffff5f",
    "brightyellow": "#ffff5f", "brightblue": "#7fa7ff", "brightmagenta": "#ff5fff", "brightcyan": "#5fffff",
    "brightwhite": "#ffffff",
}
BACKGROUND, FOREGROUND = "#1c1c1c", "#d4d4d4"


class Session:
    """mod running in a pseudo-terminal, its output fed to a pyte screen."""

    def __init__(self, mod, args, cwd, config):
        self.screen = pyte.Screen(COLS, ROWS)
        self.stream = pyte.ByteStream(self.screen)
        env = dict(os.environ, XDG_CONFIG_HOME=str(config), TERM="xterm-256color")
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.chdir(cwd)
            os.execve(mod, [mod] + args, env)
        fcntl.ioctl(self.fd, termios.TIOCSWINSZ, struct.pack("HHHH", ROWS, COLS, 0, 0))
        self.pump(1.0)

    def pump(self, seconds):
        end = time.time() + seconds
        while time.time() < end:
            ready, _, _ = select.select([self.fd], [], [], 0.05)
            if ready:
                try:
                    data = os.read(self.fd, 65536)
                except OSError:
                    return
                self.stream.feed(data)

    def send(self, *keys, settle=0.15):
        for k in keys:
            os.write(self.fd, k)
            self.pump(settle)

    def menu(self, *keys):
        """Esc and then `keys`; a pause first, since three quick Escs quit mod."""
        self.pump(1.2)
        self.send(ESC, settle=0.4)
        self.send(*keys)

    def type(self, text):
        for ch in text:
            self.send(ch.encode(), settle=0.03)
        self.pump(0.3)

    def close(self):
        os.kill(self.pid, 9)
        os.waitpid(self.pid, 0)


def color(name, default):
    if name == "default":
        return default
    if name in PALETTE:
        return PALETTE[name]
    return "#" + name if len(name) == 6 else default


def render(screen, path):
    regular = ImageFont.truetype(str(FONT_DIR / "DejaVuSansMono.ttf"), FONT_SIZE)
    bold = ImageFont.truetype(str(FONT_DIR / "DejaVuSansMono-Bold.ttf"), FONT_SIZE)
    cw = round(regular.getlength("M"))
    ascent, descent = regular.getmetrics()
    ch = ascent + descent + 2
    pad = 12
    img = Image.new("RGB", (COLS * cw + 2 * pad, ROWS * ch + 2 * pad), BACKGROUND)
    draw = ImageDraw.Draw(img)
    for y in range(ROWS):
        row = screen.buffer[y]
        for x in range(COLS):
            c = row[x]
            fg, bg = color(c.fg, FOREGROUND), color(c.bg, BACKGROUND)
            if c.bold and c.fg in PALETTE and not c.fg.startswith("bright"):
                fg = PALETTE.get("bright" + c.fg, fg)
            if c.reverse:
                fg, bg = bg, fg
            left, top = pad + x * cw, pad + y * ch
            if bg != BACKGROUND:
                draw.rectangle([left, top, left + cw - 1, top + ch - 1], fill=bg)
            if c.data.strip():
                draw.text((left, top + 1), c.data, font=bold if c.bold else regular, fill=fg)
            if c.underscore:
                draw.line([left, top + ch - 3, left + cw - 1, top + ch - 3], fill=fg)
    if not screen.cursor.hidden:
        cx, cy = pad + screen.cursor.x * cw, pad + screen.cursor.y * ch
        draw.rectangle([cx, cy + 1, cx + 1, cy + ch - 2], fill=FOREGROUND)
    img.save(path)
    print(f"wrote {path}")


def split_views(mod, work, config, out):
    """mod's own source in two split views: a .cpp above, its header below."""
    for name in ("reading_layout.cpp", "reading_layout.hpp"):
        shutil.copy(ROOT / "src" / "ui" / name, work / name)
    s = Session(mod, ["--no-word-wrap", "reading_layout.cpp", "reading_layout.hpp"], work, config)
    s.send(CTRL_G)
    s.type("37\r")
    s.menu(b"v", b"p")             # View > Split
    s.menu(SHIFT_DOWN, ESC)        # to the lower view
    s.menu(b"d", b"2")             # Documents > the header
    s.pump(0.5)
    s.send(CTRL_G)
    s.type("16\r")
    render(s.screen, out / "screenshot.png")
    s.close()


def undo_history(mod, work, config, out):
    """An edit, undone, and a different edit: the Undo History pane shows the branch."""
    shutil.copy(ROOT / "src" / "app" / "split_layout.cpp", work / "split_layout.cpp")
    s = Session(mod, ["split_layout.cpp"], work, config)
    s.send(CTRL_G)
    s.type("9\r")
    s.type("// Three rows a split: a text row, the menu bar and the status line.\n")
    s.type("// One split at least, on a small screen")
    for _ in range(3):              # undo "a small screen", a word at a time
        s.send(CTRL_Z, settle=0.3)
    s.type(" however small the screen.")
    s.menu(b"e", b"h")              # Edit > Undo History…
    s.pump(0.6)
    # The undone words are a closed branch (○>) above the current state (●): open it.
    rows = s.screen.display
    branch = next(y for y, r in enumerate(rows) if "○>" in r)
    current = next(y for y, r in enumerate(rows) if "●" in r)
    for _ in range(current - branch):
        s.send(UP)
    s.send(RIGHT, settle=0.4)
    render(s.screen, out / "undo-history.png")
    s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--mod", default=str(ROOT / "build" / "linux-release" / "mod"))
    parser.add_argument("--out", default=str(ROOT / "docs" / "images"))
    args = parser.parse_args()
    if not Path(args.mod).exists():
        sys.exit(f"screenshot.py: no mod at {args.mod}; build it first")
    if not (FONT_DIR / "DejaVuSansMono.ttf").exists():
        sys.exit(f"screenshot.py: the DejaVu Sans Mono font is needed in {FONT_DIR}")
    out = Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        tmp = Path(tmp)
        for scene in (split_views, undo_history):
            work, config = tmp / scene.__name__, tmp / (scene.__name__ + "-config")
            work.mkdir()
            (config / "mod").mkdir(parents=True)
            # The built-in C++ entry without its server: the same coloring, whatever is installed.
            cpp = next(e for e in json.loads((ROOT / "config" / "languages.json").read_text())["languages"] if e["id"] == "cpp")
            cpp.pop("command", None)
            (config / "mod" / "languages.json").write_text(json.dumps({"version": 1, "languages": [cpp]}))
            scene(args.mod, work, config, out)


if __name__ == "__main__":
    main()
