"""Co-op prototype: two instances on Surdana, A hosts the enemies, B follows them (drakan_coop,
docs/journal/2026-10-31/coop-design.md).

    python scripts/harness/coop_two_instances.py                  # about 70 s
    python scripts/harness/coop_two_instances.py --mode mark      # B only measures, nothing is written to its guest
    python scripts/harness/coop_two_instances.py --exe build/coop-exe/drakan.exe

Private play folders build/coop-a and build/coop-b (copied exe and DLLs, junctions for the assets) with their own memory
cards, like netplay_two_instances.py, whose helpers are reused. Both New Game into Surdana; A walks, B stands still and
takes screenshots. Output: build/harness/<name>/{a,b}/ (stdout_1.log, recipe.txt, *.png) and the `[coop` lines.
Exit status 0 when both instances exit cleanly and B logged creature matches.
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

RECIPE_A = """clock milliseconds
0 sync play 60
12000 key down W
20000 key up W
21000 key down D
23000 key up D
24000 key down W
32000 key up W
{quit_at} quit
"""
RECIPE_B = """clock milliseconds
0 sync play 60
8000 shot b_00
18000 shot b_01
28000 shot b_02
36000 shot b_03
{quit_at} quit
"""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="coop")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--mode", choices=("follow", "mark"), default="follow")
    ap.add_argument("--port", type=int, default=47810)
    ap.add_argument("--timeout", type=int, default=100)
    a = ap.parse_args()
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    (out / "a").mkdir(parents=True)
    (out / "b").mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"coop-{k}") for k in "ab"}
    quit_at = 44000
    (out / "a" / "recipe.txt").write_text(RECIPE_A.format(quit_at=quit_at), encoding="utf-8", newline="\n")
    (out / "b" / "recipe.txt").write_text(RECIPE_B.format(quit_at=quit_at - 2000), encoding="utf-8", newline="\n")
    extra = {
        "a": {"DRAKAN_COOP_HOST": str(a.port)},
        "b": {"DRAKAN_COOP_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_COOP_MODE": a.mode},
    }
    procs, logs = {}, {k: out / k / "stdout_1.log" for k in "ab"}
    t0 = time.monotonic()
    try:
        for k in "ab":
            env = game_env(out / k, exes[k].parent, out / k / "recipe.txt", a.timeout, extra[k])
            procs[k] = subprocess.Popen(
                [str(exes[k])], cwd=exes[k].parent, env=env, stdout=logs[k].open("wb"), stderr=subprocess.STDOUT
            )
            print(f"started {k.upper()} (pid {procs[k].pid})", flush=True)
            time.sleep(4)
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > a.timeout + 40:
                print("timeout: killing", flush=True)
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") if logs[k].is_file() else "" for k in "ab"}
    for k in "ab":
        for line in text[k].splitlines():
            if "[coop" in line:
                print(f"{k.upper()} {line.strip()}")
    ok = all(procs[k].returncode == 0 and "[in] script quit" in text[k] for k in "ab")
    matched = re.findall(r"client stats: \d+ packets, keyed \d+, matched (\d+)", text["b"])
    good = bool(matched) and max(int(m) for m in matched) > 0
    print(("PASS " if ok else "FAIL ") + "both instances exited cleanly")
    print(("PASS " if good else "FAIL ") + "b: matched host creatures")
    return 0 if ok and good else 1


if __name__ == "__main__":
    raise SystemExit(main())
