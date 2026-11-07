"""Puzzle wiring check with measured results (PASS / FAIL per scenario, exit 1 on any FAIL).

Builds a pack with the editor's own templates (story_puzzles_author.mjs, node, no browser), plays it in Stratos from a
private copy of the play folder and reads the object's words from guest RAM with DRAKAN_PLAYTEST_WATCH (door yaw, Locked
word, creature positions, Rynn's position), never a screenshot. See docs/journal/2026-11-07/door-proof.md.

    robocopy dist\\DrakanPC build\\puz-play /MIR            (once; the script then copies --exe over drakan.exe)
    python scripts/harness/puzzle_door_check.py --iso "<iso>" [--exe build/final-game/bin/drakan.exe] [--only lever_open,...]

Scenarios: lever_open, lever_toggle, walk_blocked, walk_through, plate, counter2, timer, locked_ignores_on, unlock_only,
unlock_auto, ambush, killall, keylock, keylock_nokey (Shadowmire, key picked up and used at a lock). Each takes about a minute.
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

KEY_START = "197924 210168 456966"  # Rynn's start in Shadowmire (LEVEL2RE), open ground, facing +z
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
    # stand against the plate (a solid block in Stratos): the held plate sends On, then Off when it rises again
    "plate_hold": PRE.replace("961906", "962880") + "26000 quit\n",
    # Shadowmire start (open ground): the first E taps pick the key up, then Rynn is placed 250 units from the lock (inside the
    # 327 units the Use handler walks from) and the next E taps use the key there
    "key": "clock milliseconds\n0 sync play 90\n500 place "
    + KEY_START
    + " 3.14159\n"
    + "".join(f"{15000 + i * 2000} key tap E\n" for i in range(4))
    + "24000 place 197924 210168 457416 3.14159\n"
    + "".join(f"{31000 + i * 2000} key tap E\n" for i in range(8))
    + "50000 quit\n",
    # stand against the plate until the door is open, then walk away from it (S) and stand clear: the plate rises, Off
    "plate_walk_off": PRE.replace("961906", "962880") + "12000 key down S\n16000 key up S\n30000 quit\n",
    # bow kill: pick the bow up in front of Rynn, open the inventory, equip it, close it, then fire at the dormant copies
    # (AUTOAIM turns her to the nearest living skeleton; they stand 2300 units away, so only arrows can reach them)
    "bow": PRE
    + "7200 key tap E\n8500 key tap ENTER\n"
    + "10000 key tap TAB\n13000 key tap ENTER\n14000 key tap RIGHT\n15000 key tap E\n17000 key tap TAB\n"
    + "".join(f"{20000 + i * 2500} button down 1\n{21500 + i * 2500} button up 1\n" for i in range(24))
    + "85000 quit\n",
    # walk at the dormant copies, then tap attack (AUTOAIM turns Rynn to the nearest living skeleton)
    "melee": PRE
    + "6500 key down W\n8500 key up W\n"
    + "".join(f"{9500 + i * 800} button down 1\n{9650 + i * 800} button up 1\n" for i in range(80))
    + "75000 quit\n",
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
    env: dict = field(default_factory=dict)
    level: str = "8"  # play_run level argument
    start: str = START


def _closed_after_open(r: Run) -> tuple[bool, str]:
    i = next((k for k, y in enumerate(r.door_yaw) if y > OPEN_YAW), None)
    ok = i is not None and min(r.door_yaw[i:]) < 0.3
    return ok, f"yaw max {max(r.door_yaw, default=0):.2f}, last {r.door_yaw[-1] if r.door_yaw else 0:.2f}"


def _walked_off(r: Run) -> tuple[bool, str]:
    """Open while she stands at the plate, shut after she has walked back from it."""
    ok, detail = _closed_after_open(r)
    back = max((p[2] for p in r.player), default=0) - min((p[2] for p in r.player[-10:]), default=0)
    return ok and back > 500, f"{detail}, Rynn ended {back} units back from the plate"


def _walk(blocked: bool) -> Callable[[Run], tuple[bool, str]]:
    def f(r: Run) -> tuple[bool, str]:
        zs = [p[2] for p in r.player]
        top = max(zs, default=0)
        # the door stands at z 966406: blocked = she never gets past it, through = she ends beyond it
        return (
            top < 966100 if blocked else top > 968000
        ), f"Rynn's highest z {top} (door at 966406), door open={opened(r)}"

    return f


def _bow(r: Run, expect_open: bool) -> tuple[bool, str]:
    """Rynn stays near her start (z 961906), so any kill came from range; the door opens only on two kills."""
    zs = [p[2] for p in r.player]
    walked = max(zs, default=0) - 961906
    ok = opened(r) == expect_open and walked < 1500
    return (
        ok,
        f"door open={opened(r)} (expected {expect_open}), Rynn's furthest advance {walked} units, yaw max {max(r.door_yaw, default=0):.2f}",
    )


def _ambush(r: Run) -> tuple[bool, str]:
    moved = max((abs(p[2] - t[0][2]) + abs(p[0] - t[0][0]) for t in r.creatures.values() for p in t[5:]), default=0)
    return moved > 800, f"{len(r.creatures)} creatures, furthest one moved {moved} units from its dormant spot"


KEY_OPTS = {
    "level": "LEVEL2RE",
    "start": [197924, 210168, 456966],
    "doorClass": "GF Wood Door01",
    "locked": 1,
    "autoOpen": 1,
    "keyClass": "Hermit Key.",
    "doorDz": 3000,
    "code": 62,
}
KEY_WATCH = "near=3500,player=1,words=0,data=72,max=40"

SCENARIOS = [
    Scenario(
        "keylock",
        "key",
        KEY_OPTS,
        RECIPES["key"],
        lambda r: (
            opened(r),
            f"Rynn picks the key up, uses it at the lock, the door swings (yaw max {max(r.door_yaw, default=0):.2f})",
        ),
        watch=KEY_WATCH,
        level="2",
        start=KEY_START + " 3.14159",
    ),
    Scenario(
        "keylock_nokey",
        "key",
        {**KEY_OPTS, "noKey": 1},
        RECIPES["key"],
        lambda r: (
            not opened(r),
            f"no key placed, E at the lock does nothing (yaw max {max(r.door_yaw, default=0):.2f})",
        ),
        watch=KEY_WATCH,
        level="2",
        start=KEY_START + " 3.14159",
    ),
    Scenario(
        "plate_hold",
        "plate",
        {"locked": 0, "autoOpen": 0, "hold": 1},
        RECIPES["plate_hold"],
        _closed_after_open,
    ),
    Scenario(
        "killall_melee",
        "killall",
        {"locked": 0, "autoOpen": 0, "killMsg": 1},
        RECIPES["melee"],
        lambda r: (opened(r), f"two real sword kills, yaw max {max(r.door_yaw, default=0):.2f}"),
        env={"DRAKAN_PLAYTEST_AUTOAIM": SKELETON_CLASS},
    ),
    Scenario(
        "plate_walk_off",
        "plate",
        {"locked": 0, "autoOpen": 0, "hold": 1},
        RECIPES["plate_walk_off"],
        _walked_off,
    ),
    Scenario(
        "killall_bow",
        "killall",
        {"locked": 0, "autoOpen": 0, "killMsg": 1, "bow": "Bow", "killDz": 2300},
        RECIPES["bow"],
        lambda r: _bow(r, True),
        env={"DRAKAN_PLAYTEST_AUTOAIM": SKELETON_CLASS},
    ),
    Scenario(  # control: the same shots without a bow in reach (the sword cannot reach them) leave the door shut
        "killall_bow_nobow",
        "killall",
        {"locked": 0, "autoOpen": 0, "killMsg": 1, "killDz": 2300},
        RECIPES["bow"],
        lambda r: _bow(r, False),
        env={"DRAKAN_PLAYTEST_AUTOAIM": SKELETON_CLASS},
    ),
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
        s.level,
        "--recipe",
        str(recipe),
        "--env",
        f"DRAKAN_MODS={mods}",
        "--env",
        f"DRAKAN_PLAYTEST_POS={s.start}",
        "--env",
        f"DRAKAN_PLAYTEST_WATCH={s.watch}",
        "--env",
        "DRAKAN_PLAYTEST_GOD=1",
        "--timeout",
        "400",
    ]
    for k, v in s.env.items():
        cmd += ["--env", f"{k}={v}"]
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
