"""Co-op dragons in a ground level and with three players (docs/journal/2026-11-01/coop-dragons-ground.md).

    python scripts/harness/coop_dragon_ground.py --scenario mount  --name dg_mount          # Surdana, 2 players
    python scripts/harness/coop_dragon_ground.py --scenario three-ground --name dg_three_g  # Surdana, 3 players
    python scripts/harness/coop_dragon_ground.py --scenario three-ravens --name dg_three_r  # Ravenshold, 3 players

Scenarios (base port 28000, every instance on its own private folder / memory card, nothing of the player's is touched):
  mount         A hosts a co-op match in Surdana, B joins. A's Arokh is parked at y 200000 and cannot be reached by
                script, so A *publishes* (DRAKAN_COOP_FAKE_MOUNT, a test aid in drakan_netplay.cpp) a mount at 8 s, a
                dismount at 18 s, a second mount at 26 s and a second dismount at 36 s. B (a real receiver) must show the
                dragon plus rider while mounted, keep the dragon parked while A is on foot and reuse it on the re-mount.
  three-ground  A, B and C all publish a fake mount (different offsets): each instance must show the other two dragons.
  three-ravens  A, B and C start really mounted in Ravenshold: each instance must show two partner dragons
                (DRAKAN_DRAGON_TEST_SPREAD draws partner N at N times the offset so the dragons do not overlap).
Output: build/harness/<name>/{a,b,c}/ (stdout_1.log, recipe.txt, *.png). Exit status 0 when every check passed.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import ROOT, SURDANA, game_env, prepare_folder

NL = "\n"
RAVENSHOLD = "Level 4A - Ravenshold"


def recipe(shot: str, shots: int, turn: bool) -> str:
    out = "clock milliseconds" + NL + "0 sync play 60" + NL
    for i in range(shots):
        out += f"{1500 + i * 1500} shot {shot}{i:02d}{NL}"
    if turn:  # a slow look around so partners behind the start view are seen as well
        for i in range(10):
            out += f"{4000 + i * 3000} mouse 6 0 12{NL}"
    out += f"{1500 + shots * 1500 + 2000} quit{NL}"
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--scenario", choices=["mount", "three-ground", "three-ravens", "real"], default="mount")
    ap.add_argument("--name", default=None)
    ap.add_argument("--port", type=int, default=28000)
    ap.add_argument(
        "--pos", default="737300 200000 198858 0", help="real: DRAKAN_PLAYTEST_POS for A (near Surdana's Arokh)"
    )
    ap.add_argument("--press", default="14000,34000", help="real: ms at which A taps E (call / mount, then dismount)")
    ap.add_argument("--shots", type=int, default=28)
    ap.add_argument("--raw", action="append", default=[], help='real: extra recipe line for A, e.g. "12000 key down W"')
    ap.add_argument("--level", default=SURDANA, help='real: START_LEVEL, e.g. "Level 4B - Northern Tundra"')
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--timeout", type=int, default=120)
    ap.add_argument("--turn", action="store_true", help="every instance looks around slowly")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment, all instances")
    a = ap.parse_args()
    name = a.name or "dg_" + a.scenario.replace("-", "_")

    three = a.scenario.startswith("three")
    keys = "abc" if three else "ab"
    out = ROOT / "build" / "harness" / name
    if out.exists():
        shutil.rmtree(out)
    for k in keys:
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"coopdg-{k}") for k in keys}
    shots = a.shots if not three else 18
    for k in keys:
        text_ = recipe(f"{k}_s", shots, a.turn)
        if a.scenario == "real" and k == "a":  # taps of E (use / call Arokh) on top of the screenshots
            lines = text_.splitlines()
            quit_line = next(ln for ln in lines if ln.endswith(" quit"))
            body = [ln for ln in lines if ln != quit_line]
            for ms in (int(x) for x in a.press.split(",") if x):
                body += [f"{ms} key down E", f"{ms + 1000} key up E"]
            body += a.raw
            head, rest = body[:2], sorted(body[2:], key=lambda ln: int(ln.split()[0]))
            text_ = NL.join(head + rest + [quit_line]) + NL
        (out / k / "recipe.txt").write_text(text_, encoding="utf-8", newline="\n")

    common = {"DRAKAN_PVP": "0", "DRAKAN_GHOST_LOG": "1", "DRAKAN_NET_TRACE": "1", "DRAKAN_DRAGON_PROBE": "1"}
    per: dict[str, dict[str, str]] = {k: {} for k in keys}
    if a.scenario == "mount":
        common["DRAKAN_START_LEVEL"] = SURDANA
        per["a"] = {"DRAKAN_COOP_FAKE_MOUNT": "8,18,26,36", "DRAKAN_COOP_FAKE_MOUNT_OFFSET": "700,0,0"}
    elif a.scenario == "real":
        common["DRAKAN_START_LEVEL"] = a.level
        per["a"] = {"DRAKAN_PLAYTEST_POS": a.pos} if a.pos != "none" else {}
    elif a.scenario == "three-ground":
        common["DRAKAN_START_LEVEL"] = SURDANA
        per["a"] = {"DRAKAN_COOP_FAKE_MOUNT": "8,60", "DRAKAN_COOP_FAKE_MOUNT_OFFSET": "700,0,0"}
        per["b"] = {"DRAKAN_COOP_FAKE_MOUNT": "9,60", "DRAKAN_COOP_FAKE_MOUNT_OFFSET": "-700,0,300"}
        per["c"] = {"DRAKAN_COOP_FAKE_MOUNT": "10,60", "DRAKAN_COOP_FAKE_MOUNT_OFFSET": "0,0,-900"}
    else:
        common["DRAKAN_START_LEVEL"] = RAVENSHOLD
        common["DRAKAN_DRAGON_TEST_OFFSET"] = "1100,150,0"
        common["DRAKAN_DRAGON_TEST_SPREAD"] = "1"

    env: dict[str, dict[str, str]] = {}
    for k in keys:
        e = {**common, **per[k], "DRAKAN_NET_NAME": k.upper() + "-player"}
        if k == "a":
            e.update({"DRAKAN_NET_HOST": str(a.port), "DRAKAN_NET_MODE": "coop"})
        else:
            e["DRAKAN_NET_CONNECT"] = f"127.0.0.1:{a.port}"
        for kv in a.env:
            e[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
        env[k] = e

    logs = {k: out / k / "stdout_1.log" for k in keys}
    procs: dict[str, subprocess.Popen] = {}
    t0 = time.monotonic()
    try:
        for k in keys:
            e = game_env(out / k, exes[k].parent, out / k / "recipe.txt", a.timeout, env[k], None)
            procs[k] = subprocess.Popen(
                [str(exes[k])], cwd=exes[k].parent, env=e, stdout=logs[k].open("wb"), stderr=subprocess.STDOUT
            )
            print(f"started {k.upper()} (pid {procs[k].pid})", flush=True)
            time.sleep(4)
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > a.timeout + 60:
                print("timeout: killing the processes this script started", flush=True)
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") if logs[k].is_file() else "" for k in keys}
    pat = r"link check|dragon of player|ghost of player|spawn refused|parked|re-mounted"
    for k in keys:
        for line in text[k].splitlines():
            if re.search(pat, line):
                print(f"{k.upper()} {line.strip()}")

    checks: dict[str, bool] = {
        "all exited cleanly": all(procs[k].returncode == 0 and "[in] script quit" in text[k] for k in keys)
    }
    observers = "b" if a.scenario == "mount" else keys  # in `mount` only A publishes a dragon
    for k in observers:
        spawned = set(re.findall(r"dragon of player (\d+) spawned", text[k]))
        checks[f"{k}: dragons of {(len(keys) - 1 if a.scenario != 'mount' else 1)} partner(s) spawned"] = len(
            spawned
        ) == (len(keys) - 1 if a.scenario != "mount" else 1)
        riders = set(re.findall(r"ghost of player (\d+) '[^']*' spawned", text[k]))
        checks[f"{k}: partner bodies (riders) spawned"] = len(riders) == (len(keys) - 1 if a.scenario != "mount" else 1)
        checks[f"{k}: no dragon vanished, local link unchanged"] = "CHANGED" not in text[k] and not re.search(
            r"dragon of player \d+ vanished", text[k]
        )
    if a.scenario == "mount":
        n_spawn = len(re.findall(r"dragon of player \d+ spawned", text["b"]))
        n_park = len(re.findall(r"dragon of player \d+ parked", text["b"]))
        n_re = len(re.findall(r"dragon of player \d+ re-mounted", text["b"]))
        checks["b: dragon spawned once, parked twice, re-mounted once"] = (n_spawn, n_park, n_re) == (1, 2, 1)
    for name_, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + name_)
    print(f"screenshots: {out}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
