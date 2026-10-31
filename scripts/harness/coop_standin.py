"""Stand-in character close-up: one instance, DRAKAN_GHOST_MIRROR=1 shows a stand-in beside the player that replays
the local pose 1 s late (no network) and, with DRAKAN_GHOST_ACTS, plays each activity's clip in turn (3 s each:
attack, hit, death ... the mapping the co-op ghost of a non-Rynn partner uses, docs/journal/2026-10-31/coop-stand-ins.md).

    python scripts/harness/coop_standin.py --name sin1 --char 1798 --acts idle,run,attack,hit,death --offset 0,300

The dagger save fixture (Rynn holds a dagger, so the weapon path runs too); output build/harness/<name>/*.png and the
`[ghost]` log lines (clip changes per activity, weapon attach). Private play folder build/<folder>.
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
from coop_two_instances import DEFAULT_CARD, EQUIP, make_fixture
from netplay_two_instances import ROOT, game_env, prepare_folder

SPOT = "475700,160700,487600"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="sin1")
    ap.add_argument("--folder", default="coopstand")
    ap.add_argument("--char", default="1798", help="roster class id (hex)")
    ap.add_argument("--acts", default="idle,run,attack,hit,death,block")
    ap.add_argument("--offset", default="0,300", help="x,z of the stand-in beside the player")
    ap.add_argument("--shots", type=int, default=30, help="one screenshot per second")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--env", action="append", default=[], metavar="K=V")
    a = ap.parse_args()
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    card = make_fixture(DEFAULT_CARD, "si", SPOT)
    exe = prepare_folder(a.src, a.exe, ROOT / "build" / a.folder)
    nl = chr(10)
    rec = "clock milliseconds" + nl + "0 sync play 60" + nl + EQUIP  # equips the dagger (the mirror replays it)
    for i in range(a.shots):
        rec += f"{24000 + i * 1000} shot s{i:02d}{nl}"
    rec += f"{24000 + a.shots * 1000 + 1000} quit{nl}"
    (out / "recipe.txt").write_text(rec, encoding="utf-8", newline=nl)
    env = {
        "DRAKAN_GHOST_MIRROR": "1",
        "DRAKAN_GHOST_CHAR": a.char,
        "DRAKAN_GHOST_ACTS": a.acts,
        "DRAKAN_GHOST_OFFSET": a.offset,
        "DRAKAN_GHOST_DELAY_TICKS": "10",
        "DRAKAN_GHOST_LOG": "1",
    }
    for kv in a.env:
        env[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    timeout = 60 + a.shots
    e = game_env(out, exe.parent, out / "recipe.txt", timeout, env, card)
    log = out / "stdout_1.log"
    p = subprocess.Popen([str(exe)], cwd=exe.parent, env=e, stdout=log.open("wb"), stderr=subprocess.STDOUT)
    print(f"started (pid {p.pid})", flush=True)
    t0 = time.monotonic()
    try:
        while p.poll() is None and time.monotonic() - t0 < timeout + 40:
            time.sleep(1)
    finally:
        if p.poll() is None:
            p.kill()
    text = log.read_bytes().decode("latin-1")
    for line in text.splitlines():
        if re.search(r"\[ghost\] (spawn|id \d+ channels|id \d+ hand|weapon|id \d+ stand-in)", line):
            print(line.strip())
    print(f"exit {p.returncode}; {out}")
    return 0 if p.returncode == 0 else 1


if __name__ == "__main__":
    raise SystemExit(main())
