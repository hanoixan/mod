#!/usr/bin/env python3
"""Generate src/text/width_table.inc (and optionally tests/grapheme_break_cases.inc)
from the Unicode Character Database. Python 3.10+, standard library only."""

from __future__ import annotations

import argparse
import re
import sys
import tempfile
import urllib.request
from pathlib import Path

MAX_CP = 0x10FFFF

# Relative paths below https://www.unicode.org/Public/<ver>/ucd/.
UCD_FILES = {
    "eaw": "EastAsianWidth.txt",
    "gc": "extracted/DerivedGeneralCategory.txt",
    "emoji": "emoji/emoji-data.txt",
    "gcb": "auxiliary/GraphemeBreakProperty.txt",
    "dcp": "DerivedCoreProperties.txt",
    "gbt": "auxiliary/GraphemeBreakTest.txt",
}

GCB_NAMES = {
    "CR": "GB_CR",
    "LF": "GB_LF",
    "Control": "GB_CONTROL",
    "Extend": "GB_EXTEND",
    "ZWJ": "GB_ZWJ",
    "Regional_Indicator": "GB_REGIONAL_INDICATOR",
    "Prepend": "GB_PREPEND",
    "SpacingMark": "GB_SPACINGMARK",
    "L": "GB_L",
    "V": "GB_V",
    "T": "GB_T",
    "LV": "GB_LV",
    "LVT": "GB_LVT",
}

INCB_FLAGS = {
    "Linker": "GF_INCB_LINKER",
    "Consonant": "GF_INCB_CONSONANT",
    "Extend": "GF_INCB_EXTEND",
}

LINE_RE = re.compile(r"^([0-9A-F]{4,6})(?:\.\.([0-9A-F]{4,6}))?\s*;\s*([^#]*?)\s*(?:#.*)?$")


class FormatError(Exception):
    pass


def parse_ranges(path: Path):
    """Yield (first, last, [fields...]) for every data line; fail on anything unrecognized."""
    with path.open(encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.rstrip("\n")
            stripped = line.strip()
            if not stripped or stripped.startswith("#"):
                continue
            m = LINE_RE.match(stripped)
            if not m:
                raise FormatError(f"{path.name}:{lineno}: unrecognized line: {line!r}")
            first = int(m.group(1), 16)
            last = int(m.group(2), 16) if m.group(2) else first
            fields = [x.strip() for x in m.group(3).split(";")]
            if first > last or last > MAX_CP or not all(fields):
                raise FormatError(f"{path.name}:{lineno}: bad range or fields: {line!r}")
            yield first, last, fields


def fetch(version: str, ucd_dir: Path | None, tmp: Path) -> dict[str, Path]:
    paths: dict[str, Path] = {}
    for key, rel in UCD_FILES.items():
        if ucd_dir is not None:
            for candidate in (ucd_dir / rel, ucd_dir / Path(rel).name):
                if candidate.is_file():
                    paths[key] = candidate
                    break
            else:
                raise FileNotFoundError(f"{rel} not found under {ucd_dir}")
            continue
        url = f"https://www.unicode.org/Public/{version}/ucd/{rel}"
        dest = tmp / Path(rel).name
        print(f"fetching {url}", file=sys.stderr)
        with urllib.request.urlopen(url) as resp:
            dest.write_bytes(resp.read())
        paths[key] = dest
    return paths


def check_version(path: Path, version: str) -> None:
    """The UCD files name their version in the header; refuse a mismatched --ucd-dir."""
    short = ".".join(version.split(".")[:2])
    head = path.read_text(encoding="utf-8").splitlines()[:10]
    if not any(re.search(rf"\b{re.escape(short)}\b", line) for line in head):
        raise FormatError(f"{path.name}: header does not mention Unicode {version}")


def build_table(paths: dict[str, Path]) -> list[tuple[int, int, int, str, str, str]]:
    size = MAX_CP + 1
    gc = ["Cn"] * size
    for first, last, fields in parse_ranges(paths["gc"]):
        for cp in range(first, last + 1):
            gc[cp] = fields[0]
    eaw = ["N"] * size
    for first, last, fields in parse_ranges(paths["eaw"]):
        if fields[0] not in ("A", "F", "H", "N", "Na", "W"):
            raise FormatError(f"EastAsianWidth: unknown value {fields[0]!r}")
        for cp in range(first, last + 1):
            eaw[cp] = fields[0]
    extpict = bytearray(size)
    emoji_pres = bytearray(size)
    for first, last, fields in parse_ranges(paths["emoji"]):
        prop = fields[0]
        if prop not in ("Emoji", "Emoji_Presentation", "Emoji_Modifier",
                        "Emoji_Modifier_Base", "Emoji_Component", "Extended_Pictographic"):
            raise FormatError(f"emoji-data: unknown property {prop!r}")
        for cp in range(first, last + 1):
            if prop == "Extended_Pictographic":
                extpict[cp] = 1
            elif prop == "Emoji_Presentation":
                emoji_pres[cp] = 1
    gcb = ["Other"] * size
    for first, last, fields in parse_ranges(paths["gcb"]):
        if fields[0] not in GCB_NAMES:
            raise FormatError(f"GraphemeBreakProperty: unknown value {fields[0]!r}")
        for cp in range(first, last + 1):
            gcb[cp] = fields[0]
    incb = [""] * size
    for first, last, fields in parse_ranges(paths["dcp"]):
        if fields[0] != "InCB":
            continue
        if len(fields) != 2 or fields[1] not in INCB_FLAGS:
            raise FormatError(f"DerivedCoreProperties: unknown InCB value {fields!r}")
        for cp in range(first, last + 1):
            incb[cp] = INCB_FLAGS[fields[1]]

    entries: list[tuple[int, int, int, str, str, str]] = []
    prev = None
    for cp in range(size):
        g = gc[cp]
        if g in ("Mn", "Me", "Cf", "Cc") or gcb[cp] in ("V", "T"):
            width = 0
        elif eaw[cp] in ("W", "F") or (extpict[cp] and emoji_pres[cp]):
            width = 2
        else:
            width = 1
        if g.startswith("Z"):
            cls = "CC_SPACE"
        elif g[0] in "PS":
            cls = "CC_PUNCT"
        elif g == "Cc":
            cls = "CC_CONTROL"
        else:
            cls = "CC_WORD"
        flags = []
        if extpict[cp]:
            flags.append("GF_EXTPICT")
        if incb[cp]:
            flags.append(incb[cp])
        key = (width, cls, GCB_NAMES.get(gcb[cp], "GB_OTHER"), " | ".join(flags) or "0")
        if key == (1, "CC_WORD", "GB_OTHER", "0"):
            prev = None
            continue
        if prev == key and entries and entries[-1][1] == cp - 1:
            last_entry = entries[-1]
            entries[-1] = (last_entry[0], cp) + last_entry[2:]
        else:
            entries.append((cp, cp) + key)
        prev = key

    for a, b in zip(entries, entries[1:]):
        assert a[0] <= a[1] < b[0] <= b[1], f"unsorted or overlapping: {a} {b}"
    return entries


def render_table(version: str, entries) -> str:
    out = [
        f"// Generated by tools/gen_width_table.py — Unicode {version}. DO NOT EDIT.",
        "// One entry per maximal run of code points with identical (width, class, gcb, flags).",
        "// Sorted by `first`, disjoint, and only non-default entries",
        "// (default = width 1, class word, gcb Other, no flags).",
        "// width: 0 | 1 | 2 (controls render as ^X); cls: space|word|punct|control",
        "// gcb: Grapheme_Cluster_Break = CR|LF|Control|Extend|ZWJ|Regional_Indicator|Prepend|",
        "//      SpacingMark|L|V|T|LV|LVT|Other",
        "// flags: GF_EXTPICT (Extended_Pictographic), GF_INCB_LINKER / GF_INCB_CONSONANT /",
        "//      GF_INCB_EXTEND (Indic_Conjunct_Break)",
    ]
    for first, last, width, cls, gcb, flags in entries:
        out.append(f"{{ 0x{first:04X}, 0x{last:04X}, {width}, {cls}, {gcb}, {flags} }},")
    return "\n".join(out) + "\n"


def render_cases(version: str, path: Path) -> str:
    out = [
        f"// Generated by tools/gen_width_table.py from GraphemeBreakTest.txt — Unicode {version}. DO NOT EDIT.",
        "// One entry per test line: the code points, encoded as UTF-8, and the byte",
        "// offsets of every cluster boundary, including 0 and the total length.",
        "// The string may contain NUL bytes; its length is the last offset.",
    ]
    with path.open(encoding="utf-8") as f:
        for lineno, raw in enumerate(f, 1):
            line = raw.split("#", 1)[0].strip()
            if not line:
                continue
            tokens = line.split()
            if len(tokens) < 3 or tokens[0] != "÷" or tokens[-1] != "÷" or len(tokens) % 2 == 0:
                raise FormatError(f"GraphemeBreakTest.txt:{lineno}: unrecognized line: {raw!r}")
            data = b""
            offsets = [0]
            for i in range(1, len(tokens), 2):
                cp_text, marker = tokens[i], tokens[i + 1]
                if not re.fullmatch(r"[0-9A-F]{4,6}", cp_text) or marker not in ("÷", "×"):
                    raise FormatError(f"GraphemeBreakTest.txt:{lineno}: unrecognized line: {raw!r}")
                data += chr(int(cp_text, 16)).encode("utf-8", "surrogatepass")
                if marker == "÷":
                    offsets.append(len(data))
            literal = "".join(f"\\x{b:02X}" for b in data)
            pretty = " ".join(tokens)
            out.append(f'{{ "{literal}", {{ {", ".join(map(str, offsets))} }} }},  // {pretty}')
    return "\n".join(out) + "\n"


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--unicode", required=True, metavar="VERSION")
    group = ap.add_mutually_exclusive_group(required=True)
    group.add_argument("--out", type=Path)
    group.add_argument("--check", type=Path)
    ap.add_argument("--ucd-dir", type=Path)
    ap.add_argument("--test-out", type=Path)
    args = ap.parse_args(argv)

    with tempfile.TemporaryDirectory() as tmp:
        try:
            paths = fetch(args.unicode, args.ucd_dir, Path(tmp))
            for key in ("eaw", "gc", "emoji", "gcb", "dcp", "gbt"):
                check_version(paths[key], args.unicode)
            entries = build_table(paths)
            table = render_table(args.unicode, entries)
            cases = render_cases(args.unicode, paths["gbt"]) if args.test_out else None
        except FormatError as e:
            print(f"error: {e}", file=sys.stderr)
            return 2

    outputs = [(args.out or args.check, table)]
    if args.test_out:
        outputs.append((args.test_out, cases))
    if args.check:
        status = 0
        for path, text in outputs:
            current = path.read_text(encoding="utf-8") if path.exists() else None
            if current != text:
                print(f"{path}: differs from generated output", file=sys.stderr)
                status = 1
        return status
    for path, text in outputs:
        path.write_text(text, encoding="utf-8")
    print(f"{len(entries)} ranges", file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
