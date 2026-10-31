"""Co-op dragons: two instances in Ravenshold (both start mounted on Arokh), each sees the other's dragon with the
partner's stand-in riding it (docs/journal/2026-10-31/coop-dragons.md).

    python scripts/harness/coop_dragon.py --name drag1 --exe build/coopdrag-exe/drakan.exe --port 47912
    python scripts/harness/coop_dragon.py --name drag2 --char-b 1798 --fly-b

A hosts (DRAKAN_NET_HOST, co-op match), B joins. A climbs (Space) and flies forward (W) with the mouse turning it;
B hovers (or flies too with --fly-b). The runs start from a New Game in `Level 4A - Ravenshold` (DRAKAN_START_LEVEL) on
an empty private card, so nothing of the player's is touched. `--char-a` / `--char-b` choose the stand-in each
announces (roster class id, hex; 0 = Rynn). Exit status 0 when both exited cleanly, each log shows the partner's
dragon spawned and posed, and the local dragon link check reports "intact". Output: build/harness/<name>/{a,b}/
(stdout_1.log, recipe.txt, *.png); private play folders build/<folder>-a / -b.
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
from netplay_two_instances import ROOT, game_env, prepare_folder

NL = "\n"
RAVENSHOLD = "Level 4A - Ravenshold"


def recipe(shot: str, fly: bool, mouse: bool) -> str:
    out = "clock milliseconds" + chr(10) + "0 sync play 60" + chr(10)
    out += f"500 shot {shot}pre{NL}"
    t = 2000
    if fly:
        out += f"{t} key down SPACE{NL}{t + 1500} key up SPACE{NL}"
        if mouse:  # then forward and a slow turn, so the partner's dragon is seen from the side as well
            out += f"{t + 5000} key down W{NL}"
            for i in range(8):
                out += f"{t + 6000 + i * 1500} mouse 6 -2 12{NL}"
            out += f"{t + 19000} key up W{NL}"
    for i in range(24):
        out += f"{1500 + i * 1500} shot {shot}{i:02d}{NL}"
    out += f"{1500 + 24 * 1500 + 2000} quit{NL}"
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="drag1")
    ap.add_argument("--folder", default="coopdrag")
    ap.add_argument("--port", type=int, default=47912)
    ap.add_argument("--char-a", default="0", help="roster class id A plays as (hex; 0 = Rynn)")
    ap.add_argument("--char-b", default="0", help="roster class id B plays as (hex; 0 = Rynn)")
    ap.add_argument("--hover-a", action="store_true", help="A hovers too (use with DRAKAN_DRAGON_TEST_OFFSET)")
    ap.add_argument("--fly-b", action="store_true", help="B flies too (default: B hovers at the start)")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--timeout", type=int, default=75)
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment, both instances")
    ap.add_argument("--env-a", action="append", default=[], metavar="K=V")
    ap.add_argument("--env-b", action="append", default=[], metavar="K=V")
    a = ap.parse_args()

    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    keys = "ab"
    for k in keys:
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.folder}-{k}") for k in keys}
    recipes = {"a": recipe("a_s", not a.hover_a, True), "b": recipe("b_s", a.fly_b, False)}
    for k in keys:
        (out / k / "recipe.txt").write_text(recipes[k], encoding="utf-8", newline="\n")
    common = {
        "DRAKAN_PVP": "0",
        "DRAKAN_GHOST_LOG": "1",
        "DRAKAN_NET_TRACE": "1",
        "DRAKAN_DRAGON_PROBE": "1",
        "DRAKAN_START_LEVEL": RAVENSHOLD,
    }
    env = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_NET_MODE": "coop",
            "DRAKAN_COOP_CHARACTER": a.char_a,
        },
        "b": {
            **common,
            "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}",
            "DRAKAN_NET_NAME": "Bravo",
            "DRAKAN_COOP_CHARACTER": a.char_b,
        },
    }
    for kv in a.env + a.env_a:
        env["a"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    for kv in a.env + a.env_b:
        env["b"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    logs = {k: out / k / "stdout_1.log" for k in keys}
    procs = {}
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
            if time.monotonic() - t0 > a.timeout + 50:
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
    for k in keys:
        for line in text[k].splitlines():
            if re.search(
                r"link check|dragon of player|ghost of player|dragon probe|spawn refused|is now character", line
            ):
                print(f"{k.upper()} {line.strip()}")
    checks = {
        "both exited cleanly": all(procs[k].returncode == 0 and "[in] script quit" in text[k] for k in keys),
        "a: dragon of b spawned": "dragon of player" in text["a"] and "spawned" in text["a"],
        "b: dragon of a spawned": "dragon of player" in text["b"] and "spawned" in text["b"],
        "no local dragon link changed": all("CHANGED" not in text[k] for k in keys),
        "link check ran": any("link check (dragon ghost)" in text[k] for k in keys),
    }
    for name, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + name)
    print(f"screenshots: {out}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
