"""Targeted checks for the debug menu options a still picture cannot judge (docs/reference/debug-menu.md).

    python scripts/harness/debug_menu_checks.py --exe <play folder>/drakan.exe [--only motion,teleport,commands]

motion    per option: walk 1.5 s (W), switch it on, walk, switch it off, walk; Rynn's and the camera's distance
          (Enable Object Update, Pause, Fly, Fly PAUSE MODE, Half Speed, Speed, Single Step, the animation switches,
          Turbo Mode)
teleport  X / Y / Z = (224, 90, 168) through Right / Shift / Ctrl, then Teleport: TELE moves the camera to 2048 * X/Y/Z
commands  RAM before / after Enter for the console commands with no picture (Disable Sound, Show Damage, the AI
          displays, Drop, MrClean, Crosshair); words that change in no control pair and for no other command
Output: build/harness/dm-<check>-*/ and build/harness/dm-checks.json.
"""

from __future__ import annotations

import argparse
import json
import math
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
ENV = [
    "DRAKAN_DEBUG_MENU=1",
    "DRAKAN_DEBUG_MENU_LOG=1",
    "DRAKAN_FULLSCREEN=0",
    "DRAKAN_WINDOW_WIDTH=1280",
    "DRAKAN_WINDOW_HEIGHT=960",
    "DRAKAN_WS_ASPECT=4:3",
]
MOTION = [
    ("objupdate", 4, 1),
    ("noanim", 4, 3),
    ("nointerp", 4, 4),
    ("turbo", 2, 8),
    ("singlestep", 10, 5),
    ("fly", 11, 1),
    ("speed", 11, 9),
    ("pause", 11, 10),
    ("flypause", 11, 11),
    ("halfspeed", 11, 12),
]
COMMANDS = [
    ("nosound", 8, 0),
    ("showdmg", 10, 0),
    ("aichanges", 10, 1),
    ("aistates", 10, 2),
    ("aianim", 10, 3),
    ("aisight", 10, 4),
    ("drop", 11, 2),
    ("mrclean", 11, 3),
    ("crosshair", 11, 15),
]
DATA_LO, DATA_HI = 0x4DA400, 0x5C0000  # .data .. end of .bss
MENU_LO, MENU_HI = 0x4E8D48, 0x4E8F48  # the menu object itself


def play(name: str, recipe: str, exe: Path) -> Path:
    rec = ROOT / "build/harness" / f"{name}.txt"
    rec.parent.mkdir(parents=True, exist_ok=True)
    rec.write_text(recipe, encoding="utf-8", newline="\n")
    cmd = [sys.executable, str(ROOT / "scripts/harness/play_run.py"), "--name", name, "--exe", str(exe)]
    cmd += ["--recipe", str(rec), "--attempts", "1", "--timeout", "240"]
    for e in ENV:
        cmd += ["--env", e]
    subprocess.run(cmd, cwd=ROOT, capture_output=True)
    return ROOT / "build/harness" / name


def ram(path: Path) -> bytes:
    return path.read_bytes()


def u32(d: bytes, a: int) -> int:
    return struct.unpack_from("<I", d, a & 0x1FFFFFF)[0]


def positions(d: bytes) -> tuple:
    ctx = u32(d, 0x5565D4)
    player, camera = u32(d, ctx + 0x4B0), u32(d, ctx + 0x4B4)
    return struct.unpack_from("<3f", d, (player + 0x14) & 0x1FFFFFF), struct.unpack_from(
        "<3f", d, (camera + 0x14) & 0x1FFFFFF
    )


def values(out: Path) -> list[str]:
    log = out / "drakan.log"
    lines = log.read_text(errors="replace").splitlines() if log.exists() else []
    return [line.split("] ", 1)[1] for line in lines if "[debug-menu] value" in line]


def walk(lines: list[str], t: int, tag: str) -> int:
    lines += [f"{t} dump ram_{tag}0", f"{t + 50} key down W", f"{t + 1550} key up W"]
    lines += [f"{t + 1600} dump ram_{tag}1", f"{t + 1650} shot {tag}"]
    return t + 2400


def toggle(lines: list[str], t: int, page: int, row: int) -> int:
    """Open the menu, go to (page, row) when given (the menu keeps its place), Enter, close."""
    lines.append(f"{t} key tap F3")
    t += 400
    for _ in range(page - 1):
        lines.append(f"{t} key tap PGDN")
        t += 300
    for _ in range(row):
        lines.append(f"{t} key tap DOWN")
        t += 250
    lines += [f"{t} key tap ENTER", f"{t + 600} key tap F3"]
    return t + 1200


def motion(exe: Path) -> dict:
    res = {}
    for name, page, row in MOTION:
        lines = ["clock milliseconds", "0 sync play 60"]
        t = walk(lines, 600, "base")
        t = toggle(lines, t, page, row)
        t = walk(lines, t, "on")
        t = toggle(lines, t, 1, 0)
        t = walk(lines, t, "off")
        lines.append(f"{t} quit")
        out = play(f"dm-motion-{name}", "\n".join(lines) + "\n", exe)
        r = {}
        for tag in ("base", "on", "off"):
            try:
                (a0, c0), (a1, c1) = positions(ram(out / f"ram_{tag}0.ram")), positions(ram(out / f"ram_{tag}1.ram"))
                r[tag] = {"rynn": round(math.dist(a0, a1)), "camera": round(math.dist(c0, c1))}
            except (FileNotFoundError, struct.error) as e:
                r[tag] = str(e)
        r["values"] = values(out)
        res[name] = r
        print("motion", name, json.dumps(r))
    return res


def teleport(exe: Path) -> dict:
    lines, t = ["clock milliseconds", "0 sync play 60", "400 dump ram_t0", "800 key tap F3"], 1100
    for _ in range(8):
        lines.append(f"{t} key tap PGDN")
        t += 300
    for value in (224, 90, 168):
        lines.append(f"{t} key tap DOWN")
        t += 300
        for mod, step in (("LCONTROL", 100), ("LSHIFT", 10), (None, 1)):
            count, value = divmod(value, step)
            if not count:
                continue
            if mod:
                lines.append(f"{t} key down {mod}")
                t += 100
            for _ in range(count):
                lines.append(f"{t} key tap RIGHT")
                t += 250
            if mod:
                lines.append(f"{t} key up {mod}")
                t += 100
    for _ in range(3):
        lines.append(f"{t} key tap UP")
        t += 300
    lines += [f"{t} key tap ENTER", f"{t + 800} dump ram_t1", f"{t + 900} shot teleported", f"{t + 1300} quit"]
    out = play("dm-teleport", "\n".join(lines) + "\n", exe)
    (r0, _), (r1, c1) = positions(ram(out / "ram_t0.ram")), positions(ram(out / "ram_t1.ram"))
    res = {
        "rynn_moved": round(math.dist(r0, r1)),
        "camera": [round(v) for v in c1],
        "expected": [458752, 184320, 344064],
    }
    res["values"] = values(out)
    print("teleport", json.dumps(res))
    return res


def commands(exe: Path) -> dict:
    lines, t, page, row = ["clock milliseconds", "0 sync play 60", "500 key tap F3"], 800, 1, 0
    for tag, p, r in COMMANDS:
        while page < p:
            lines.append(f"{t} key tap PGDN")
            t, page, row = t + 300, page + 1, 0
        while row < r:
            lines.append(f"{t} key tap DOWN")
            t, row = t + 250, row + 1
        lines += [f"{t} dump ram_{tag}_c0", f"{t + 700} dump ram_{tag}_c1", f"{t + 800} dump ram_{tag}_a0"]
        lines += [f"{t + 900} key tap ENTER", f"{t + 1600} dump ram_{tag}_a1", f"{t + 1650} shot {tag}"]
        t += 2000
    lines.append(f"{t} quit")
    out = play("dm-cmds", "\n".join(lines) + "\n", exe)

    def words(p):
        d = ram(p)[DATA_LO:DATA_HI]
        return struct.unpack(f"<{len(d) // 4}I", d)

    dumps = {tag: {s: words(out / f"ram_{tag}_{s}.ram") for s in ("c0", "c1", "a0", "a1")} for tag, _, _ in COMMANDS}
    noisy = set()
    for d in dumps.values():
        noisy |= {i for i, (x, y) in enumerate(zip(d["c0"], d["c1"])) if x != y}
    acted = {tag: {i for i, (x, y) in enumerate(zip(d["a0"], d["a1"])) if x != y} - noisy for tag, d in dumps.items()}
    res = {}
    for tag, d in dumps.items():
        others = set().union(*(v for k, v in acted.items() if k != tag))
        own = [i for i in sorted(acted[tag] - others) if not MENU_LO <= DATA_LO + 4 * i < MENU_HI]
        res[tag] = [(hex(DATA_LO + 4 * i), hex(d["a0"][i]), hex(d["a1"][i])) for i in own[:12]]
        print("command", tag, res[tag][:6])
    return res


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT / "dist/DrakanPC/drakan.exe")
    ap.add_argument("--only", default="motion,teleport,commands")
    a = ap.parse_args()
    exe = a.exe.resolve()
    report = {}
    for check in a.only.split(","):
        report[check] = {"motion": motion, "teleport": teleport, "commands": commands}[check](exe)
    (ROOT / "build/harness/dm-checks.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
