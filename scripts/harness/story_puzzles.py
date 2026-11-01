"""Puzzle proof harness: builds a puzzle mod with the editor's templates (story_puzzles_author.mjs), plays it in Stratos
(DRAKAN_PLAYTEST_POS + keyboard walk), traces the class handlers (DRAKAN_TRACE_FN) and asserts the engine message ids the
handlers receive (docs/reference/level-format.md 15.1: ordinal -> engine id table at 0x4fbc40).

    python scripts/harness/story_puzzles.py --exe build/final-game/bin/drakan.exe --iso <iso> [--kinds plate,counter]

Handlers traced: door 45ba20, plate 45de10, counter 396428, lever 416170, timer 395e48, key lock 353550, cut scene 4091a0,
visibility toggler 28d430. Event 20 = message, a3 -> u16 engine id.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
HANDLERS = {
    "45ba20": "door",
    "45de10": "plate",
    "396428": "counter",
    "416170": "lever",
    "395e48": "timer",
    "353550": "keylock",
}
# kind -> (player start, expected (handler, engine id) pairs that must be received)
START = "651612 147580 961906 3.14159"  # Rynn's start in Stratos, facing +z
# kind -> (key events after the "before" shot, expected (handler, engine id) pairs, author options as JSON)
KINDS = {
    "plate": (
        "8500 key down W\n10500 key up W",
        [("counter", 0x1001), ("door", 0x1102)],
        '{"locked":1,"autoOpen":1,"then":"Unlock"}',
    ),
    "counter": ("8500 key down W\n12500 key up W", [("counter", 0x1001), ("door", 0x1001)], "{}"),
    "lever": ("8500 key down W\n9300 key up W\n9800 key down E\n10000 key up E", [("door", 0x1001)], "{}"),
    "timer": ("8500 key down W\n9500 key up W", [("timer", 0x1001), ("door", 0x1001)], "{}"),
}
RECIPE = (
    "clock milliseconds\n0 sync play 90\n8000 shot before\n{keys}\n14500 shot after1\n18000 shot after2\n19000 quit\n"
)
EV20 = re.compile(r"^\[fn ([0-9a-f]+) t\d+\].* a1=14 .*\[a3\]=([0-9a-f]+),", re.M)


def messages(log: str) -> list[tuple[str, int]]:
    out = []
    for m in re.finditer(r"^\[fn ([0-9a-f]+) t\d+\].* a1=20 .*\[a3\]=([0-9a-f]+),", log, re.M):
        out.append((HANDLERS.get(m.group(1), m.group(1)), int(m.group(2), 16) & 0xFFFF))
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, default=ROOT / "build/final-game/bin/drakan.exe")
    ap.add_argument("--iso", required=True)
    ap.add_argument(
        "--play", type=Path, default=ROOT / "build/puz-play", help="private play folder (copy of dist/DrakanPC)"
    )
    ap.add_argument("--kinds", default="plate,counter")
    a = ap.parse_args()
    shutil.copyfile(a.exe, a.play / "drakan.exe")
    fails = 0
    for kind in a.kinds.split(","):
        keys, expect, opts = KINDS[kind]
        mods = ROOT / "build/harness" / f"puz-mods-{kind}"
        mods.mkdir(parents=True, exist_ok=True)
        env = {**__import__("os").environ, "DRAKAN_ISO": a.iso, "PUZ_OPT": opts}
        subprocess.run(
            [
                "node",
                "--experimental-transform-types",
                str(ROOT / "scripts/harness/story_puzzles_author.mjs"),
                kind,
                str(mods),
            ],
            check=True,
            env=env,
            stdout=subprocess.DEVNULL,
        )
        recipe = ROOT / "build/harness" / f"puz-{kind}.txt"
        recipe.write_text(RECIPE.format(keys=keys))
        name = f"puz-{kind}"
        subprocess.run(
            [
                sys.executable,
                str(ROOT / "scripts/harness/play_run.py"),
                "--name",
                name,
                "--exe",
                str(a.play / "drakan.exe"),
                "--level",
                "8",
                "--recipe",
                str(recipe),
                "--env",
                f"DRAKAN_MODS={mods}",
                "--env",
                f"DRAKAN_PLAYTEST_POS={START}",
                "--env",
                "DRAKAN_TRACE_FN=" + ",".join(HANDLERS),
                "--env",
                "DRAKAN_TRACE_N=100000",
                "--timeout",
                "400",
                "--attempts",
                "2",
            ],
            check=True,
        )
        log = (ROOT / "build/harness" / name / "stdout_1.log").read_text("latin-1")
        got = set(messages(log))
        for want in expect:
            ok = want in got
            fails += not ok
            print(f"[{kind}] {want[0]} receives 0x{want[1]:x}: {'OK' if ok else 'FAIL'}")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
