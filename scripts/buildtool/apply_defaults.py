#!/usr/bin/env python3
"""Set the starting values of three PC features in a play folder's ini files (the installer's "Game defaults" page).

    apply_defaults.py --play dist/DrakanPC [--menus modern|original] [--bow ootf|native] [--dual-wield on|off]

* --menus   drakan.ini `title_menu`: `modern` (the PC main menu over the title art) or `original` (= `native`, the game's own menu).
* --bow     drakan_input.ini `bow_aim`: `ootf` (Order of the Flame bow combat: left click fires with auto-aim, right click toggles the
            sight) or `native` (the original PS2 bow).
* --dual-wield  drakan.ini `dual_wield`: `on` / `off` (experimental).

These are only starting values: every one can be changed in the game's PC settings (F1) or in the ini files afterwards. Only the
named values are touched; comments, other keys and line endings are kept. Standard library only.
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

MENUS = {"modern": "modern", "original": "native"}
BOW = {"ootf": "ootf", "native": "native"}
DUAL = {"on": "1", "off": "0"}


def set_key(text: str, key: str, value: str) -> tuple[str, str | None]:
    """text with the first `key = value` line set to `value`; (new text, previous value) or (text, None) when absent."""
    lines = text.splitlines(keepends=True)
    pattern = re.compile(rf"^(\s*{re.escape(key)}\s*=\s*)([^;\r\n]*?)(\s*(?:;[^\r\n]*)?)(\r?\n?)$", re.IGNORECASE)
    for index, line in enumerate(lines):
        match = pattern.match(line)
        if match:
            lines[index] = f"{match.group(1)}{value}{match.group(3)}{match.group(4)}"
            return "".join(lines), match.group(2).strip()
    return text, None


def apply_file(path: Path, key: str, value: str) -> str:
    """Set one key in one ini; returns a report line. A missing file or key is reported, never fatal."""
    if not path.is_file():
        return f"{path.name}: not found, {key} left alone"
    raw = path.read_bytes()
    bom = raw.startswith(b"\xef\xbb\xbf")
    text = raw.decode("utf-8-sig")  # bytes, so CRLF files stay CRLF
    new, previous = set_key(text, key, value)
    if previous is None:
        return f"{path.name}: no {key} line, left alone"
    if new != text:
        path.write_bytes((b"\xef\xbb\xbf" if bom else b"") + new.encode("utf-8"))
    return f"{path.name}: {key} = {value}" + (f" (was {previous})" if previous != value else "")


def apply(play: Path, menus: str = "", bow: str = "", dual: str = "") -> list[str]:
    report = []
    if menus:
        report.append(apply_file(play / "drakan.ini", "title_menu", MENUS[menus]))
    if dual:
        report.append(apply_file(play / "drakan.ini", "dual_wield", DUAL[dual]))
    if bow:
        report.append(apply_file(play / "drakan_input.ini", "bow_aim", BOW[bow]))
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--play", required=True, type=Path)
    parser.add_argument("--menus", choices=sorted(MENUS), default="")
    parser.add_argument("--bow", choices=sorted(BOW), default="")
    parser.add_argument("--dual-wield", choices=sorted(DUAL), default="", dest="dual")
    args = parser.parse_args(argv)
    if not args.play.is_dir():
        print(f"Play folder not found: {args.play}", file=sys.stderr)
        return 1
    for line in apply(args.play, args.menus, args.bow, args.dual):
        print(line)
    return 0


if __name__ == "__main__":
    sys.exit(main())
