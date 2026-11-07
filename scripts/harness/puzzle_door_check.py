"""Puzzle wiring check with measured results (PASS / FAIL per scenario, exit 1 on any FAIL).

Builds a pack with the editor's own templates (story_puzzles_author.mjs, node, no browser), plays it in Stratos from a
private copy of the play folder and reads the object's words from guest RAM with DRAKAN_PLAYTEST_WATCH (door yaw, Locked
word, creature positions, Rynn's position), never a screenshot. See docs/journal/2026-11-07/door-proof.md.

    robocopy dist\\DrakanPC build\\puz-play /MIR            (once; the script then copies --exe over drakan.exe)
    python scripts/harness/puzzle_door_check.py --iso "<iso>" [--exe build/final-game/bin/drakan.exe] [--only lever_open,...]

Scenarios: lever_open, lever_toggle, walk_blocked, walk_through, plate, counter2, timer, locked_ignores_on, unlock_only,
unlock_auto, ambush, killall. Each takes about a minute.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from collections.abc import Callable
from dataclasses import dataclass, field
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
START = "651612 147580 961906 3.14159"  # Rynn's start in Stratos, facing +z; the door is 4500 units ahead
WATCH_LINE = re.compile(
    r"^\[watch ([0-9.]+)\] ref=(\d+) h=\w+ cls=(\w+) type=\d+ pos=(-?\d+),(-?\d+),(-?\d+) yaw=(-?[0-9.]+)(.*)$"
)
DOOR_CLASS, SKELETON_CLASS = "10013", "10031"
OPEN_YAW = 1.4  # a door counts as open past this (it turns about 1.7 rad)

PRE = "clock milliseconds\n0 sync play 90\n500 place 651612 147580 961906 3.14159\n"
RECIPES = {
    "press": PRE + "6500 key tap E\n16000 quit\n",
    "press_twice": PRE + "6500 key tap E\n19000 key tap E\n27000 quit\n",
    "press_near": PRE + "6500 key tap E\n10000 place 652400 147580 964500 3.14159\n17000 quit\n",
    "walk_lane": PRE
    + "6500 key tap E\n10000 place 652600 147580 963400 3.14159\n16000 key down W\n23000 key up W\n24000 quit\n",
    "walk_lane_no_press": PRE
    + "10000 place 652600 147580 963400 3.14159\n16000 key down W\n23000 key up W\n24000 quit\n",
    "walk": PRE + "6500 key down W\n12500 key up W\n22000 quit\n",
    "counter": PRE + "6500 place 651612 147580 962880 3.14159\n13000 place 651612 147580 964480 3.14159\n20000 quit\n",
    "idle": PRE + "22000 quit\n",
}


@dataclass
class Run:
    door_yaw: list[float] = field(default_factory=list)
    door_words: list[str] = field(default_factory=list)  # the instance data part of each door line
    player: list[tuple[int, int, int]] = field(default_factory=list)
    creatures: dict[str, list[tuple[int, int, int]]] = field(default_factory=dict)
    messages: set[tuple[str, int]] = field(default_factory=set)  # (handler, engine id) seen as event 0x20


def parse(log: str) -> Run:
    r = Run()
    for line in log.splitlines():
        m = WATCH_LINE.match(line)
        if m:
            _, ref, cls, x, y, z, yaw, rest = m.groups()
            if cls == DOOR_CLASS:
                r.door_yaw.append(float(yaw))
                r.door_words.append(rest.split("| d+")[-1] if "| d+" in rest else "")
            elif cls == SKELETON_CLASS:
                r.creatures.setdefault(ref, []).append((int(x), int(y), int(z)))
            elif ref in ("22", "17"):  # Rynn
                r.player.append((int(x), int(y), int(z)))
            continue
        m = re.match(r"^\[fn ([0-9a-f]+) t\d+\].* a1=20 .*\[a3\]=([0-9a-f]+),", line)
        if m:
            r.messages.add((m.group(1), int(m.group(2), 16) & 0xFFFF))
    return r


def opened(r: Run) -> bool:
    return bool(r.door_yaw) and max(r.door_yaw) > OPEN_YAW


def locked(r: Run) -> bool:
    return bool(r.door_words) and bool(re.search(r"(^| )40=1( |$)", r.door_words[-1]))


@dataclass
class Scenario:
    name: str
    kind: str  # author kind
    opts: dict
    recipe: str
    check: Callable[[Run], tuple[bool, str]]
    watch: str = f"class={DOOR_CLASS},player=1,words=0,data=24,max=4"
    trace: str = ""


def _closed_after_open(r: Run) -> tuple[bool, str]:
    i = next((k for k, y in enumerate(r.door_yaw) if y > OPEN_YAW), None)
    ok = i is not None and min(r.door_yaw[i:]) < 0.3
    return ok, f"yaw max {max(r.door_yaw, default=0):.2f}, last {r.door_yaw[-1] if r.door_yaw else 0:.2f}"


def _walk(blocked: bool) -> Callable[[Run], tuple[bool, str]]:
    def f(r: Run) -> tuple[bool, str]:
        zs = [p[2] for p in r.player]
        top = max(zs, default=0)
        # the door stands at z 966406: blocked = she never gets past it, through = she ends beyond it
        return (
            top < 966100 if blocked else top > 968000
        ), f"Rynn's highest z {top} (door at 966406), door open={opened(r)}"

    return f


def _ambush(r: Run) -> tuple[bool, str]:
    moved = max((abs(p[2] - t[0][2]) + abs(p[0] - t[0][0]) for t in r.creatures.values() for p in t[5:]), default=0)
    return moved > 800, f"{len(r.creatures)} creatures, furthest one moved {moved} units from its dormant spot"


SCENARIOS = [
    Scenario(
        "lever_open",
        "lever",
        {"locked": 0, "autoOpen": 0},
        RECIPES["press"],
        lambda r: (opened(r), f"yaw max {max(r.door_yaw, default=0):.2f}"),
    ),
    Scenario("lever_toggle", "lever", {"locked": 0, "autoOpen": 0}, RECIPES["press_twice"], _closed_after_open),
    Scenario("walk_blocked", "lever", {"locked": 0, "autoOpen": 0}, RECIPES["walk_lane_no_press"], _walk(True)),
    Scenario("walk_through", "lever", {"locked": 0, "autoOpen": 0}, RECIPES["walk_lane"], _walk(False)),
    Scenario(
        "plate",
        "plate",
        {"locked": 0, "autoOpen": 0, "then": "On"},
        RECIPES["walk"],
        lambda r: (opened(r), f"yaw max {max(r.door_yaw, default=0):.2f}"),
    ),
    Scenario(
        "counter2",
        "counter",
        {"locked": 0, "autoOpen": 0, "then": "On"},
        RECIPES["counter"],
        lambda r: (opened(r), f"yaw max {max(r.door_yaw, default=0):.2f}"),
    ),
    Scenario(
        "timer",
        "timer",
        {"locked": 0, "autoOpen": 0},
        RECIPES["walk"],
        lambda r: (opened(r), f"yaw max {max(r.door_yaw, default=0):.2f}"),
    ),
    Scenario(
        "locked_ignores_on",
        "lever",
        {"locked": 1, "autoOpen": 0},
        RECIPES["press"],
        lambda r: (not opened(r) and locked(r), f"yaw max {max(r.door_yaw, default=0):.2f}, still locked {locked(r)}"),
    ),
    Scenario(
        "unlock_only",
        "lever",
        {"locked": 1, "autoOpen": 0, "leverMode": "unlock"},
        RECIPES["press_near"],
        lambda r: (not opened(r) and not locked(r), f"yaw max {max(r.door_yaw, default=0):.2f}, locked {locked(r)}"),
    ),
    Scenario(
        "unlock_auto",
        "lever",
        {"locked": 1, "autoOpen": 1, "leverMode": "unlock"},
        RECIPES["press_near"],
        lambda r: (opened(r), f"yaw max {max(r.door_yaw, default=0):.2f}"),
    ),
    Scenario(
        "ambush",
        "ambush",
        {"ambushDz": 2800, "radius": 900},
        RECIPES["walk"],
        _ambush,
        watch=f"class={SKELETON_CLASS},near=9000,max=8,words=0,data=0",
    ),
    Scenario(
        "killall",
        "killall",
        {"locked": 0, "autoOpen": 0},
        RECIPES["walk"],
        lambda r: (
            opened(r) and ("396428", 0x1003) in r.messages and ("4255c8", 0x10A3) in r.messages,
            f"yaw max {max(r.door_yaw, default=0):.2f}, messages {sorted(r.messages)[:6]}",
        ),
        trace="4255c8,396428",
    ),
]


def run_scenario(s: Scenario, a: argparse.Namespace) -> bool:
    out = ROOT / "build/harness" / f"puzdoor-{s.name}"
    mods = ROOT / "build/harness" / f"puzdoor-mods-{s.name}"
    mods.mkdir(parents=True, exist_ok=True)
    env = {**os.environ, "DRAKAN_ISO": a.iso, "PUZ_OPT": __import__("json").dumps(s.opts)}
    subprocess.run(
        [
            "node",
            "--experimental-transform-types",
            str(ROOT / "scripts/harness/story_puzzles_author.mjs"),
            s.kind,
            str(mods),
        ],
        check=True,
        env=env,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    recipe = ROOT / "build/harness" / f"puzdoor-{s.name}.txt"
    recipe.write_text(s.recipe)
    cmd = [
        sys.executable,
        str(ROOT / "scripts/harness/play_run.py"),
        "--name",
        f"puzdoor-{s.name}",
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
        f"DRAKAN_PLAYTEST_WATCH={s.watch}",
        "--env",
        "DRAKAN_PLAYTEST_GOD=1",
        "--timeout",
        "400",
    ]
    if s.trace:
        cmd += ["--env", f"DRAKAN_TRACE_FN={s.trace}", "--env", "DRAKAN_TRACE_N=100000"]
    subprocess.run(cmd, check=False, stdout=subprocess.DEVNULL)
    log = out / "stdout_1.log"
    if not log.exists():
        print(f"FAIL {s.name}: no log")
        return False
    ok, detail = s.check(parse(log.read_text("latin-1")))
    print(f"{'PASS' if ok else 'FAIL'} {s.name}: {detail}")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, default=ROOT / "build/final-game/bin/drakan.exe")
    ap.add_argument("--iso", required=True)
    ap.add_argument("--play", type=Path, default=ROOT / "build/puz-play", help="private copy of dist/DrakanPC")
    ap.add_argument("--only", default="", help="comma separated scenario names")
    a = ap.parse_args()
    shutil.copyfile(a.exe, a.play / "drakan.exe")
    only = {x for x in a.only.split(",") if x}
    results = [run_scenario(s, a) for s in SCENARIOS if not only or s.name in only]
    print(f"{sum(results)}/{len(results)} passed")
    return 0 if all(results) else 1


if __name__ == "__main__":
    sys.exit(main())
