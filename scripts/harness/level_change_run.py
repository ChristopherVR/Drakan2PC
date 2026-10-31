"""Level change from the host side in the running game (drakan_levelchange, docs/reference/world-model.md section 10,
docs/journal/2026-10-31/level-change.md).

    python scripts/harness/level_change_run.py                       # all cases (about 4 game runs, 70-100 s each)
    python scripts/harness/level_change_run.py --case city           # one case
    python scripts/harness/level_change_run.py --exe build/final-game/bin/drakan.exe

Every case runs scripts/harness/play_run.py (New Game on a private memory card under build/harness/<prefix>-<case>/card,
never the player's saves) from a private play folder (build/<prefix>-play, the exe is copied there) with
DRAKAN_LEVELCHANGE=<steps> (the exit triggers of the level are fired by the game's own transition code; steps are exit
indices or destination names, comma separated, one per level) and DRAKAN_LIFECYCLE_LOG=1 / DRAKAN_WORLD=2, then checks
the log:

  city      Surdana -> City of the Damned (step "city"): the level loads, the guest keeps running to a clean quit
  lair      Surdana -> Inquisitor's Lair (step "inquisitor")
  roundtrip Surdana -> Lair -> back to Surdana through the Lair's own exit (steps "1,0")
  chain     Surdana -> City -> Surdana -> Lair (steps "city,0,inquisitor"; the second step is the City's nearest exit)

Known limits (the journal has the details): the arrival marker of some destinations does not take the player (Kragmor
falls out of the world, the Catacombs freeze), so they are not in the default cases; pass --steps to try one.
Exit status 0 when every check of every case passed.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "harness"))
import netplay_two_instances as net  # noqa: E402  (prepare_folder: private play folder, junctions to the big assets)

RECIPE = """clock milliseconds
0 sync play 60
{shots}{quit_at} quit
"""

CASES = {
    "city": ("city", ["Level 1D - City of the Damned"]),
    "lair": ("inquisitor", ["Level 1E - Inquisitor's Lair"]),
    "roundtrip": ("1,0", ["Level 1E - Inquisitor's Lair", "Level 1A - Surdana"]),
    "chain": (
        "city,0,inquisitor",
        ["Level 1D - City of the Damned", "Level 1A - Surdana", "Level 1E - Inquisitor's Lair"],
    ),
}


def run_case(a: argparse.Namespace, name: str, steps: str, expect: list[str]) -> bool:
    exe = net.prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-play")
    n_steps = len(steps.split(","))
    wait = 14 + 18 * n_steps  # seconds after "play": 1200 frames in the level + the load, per step
    shots = "".join(f"{(10 + 18 * i) * 1000} shot s{i}\n" for i in range(1, n_steps + 1))
    recipe = ROOT / "build" / "harness" / f"{a.prefix}-{name}-recipe.txt"
    recipe.parent.mkdir(parents=True, exist_ok=True)
    recipe.write_text(RECIPE.format(shots=shots, quit_at=wait * 1000), encoding="utf-8", newline="\n")
    out_name = f"{a.prefix}-{name}"
    cmd = [
        sys.executable,
        str(ROOT / "scripts" / "harness" / "play_run.py"),
        "--name", out_name,
        "--exe", str(exe),
        "--recipe", str(recipe),
        "--timeout", str(wait + 60),
        "--attempts", "1",
        "--env", f"DRAKAN_LEVELCHANGE={steps}@{a.frames}",
        "--env", "DRAKAN_LIFECYCLE_LOG=1",
        "--env", "DRAKAN_WORLD=2",
    ]  # fmt: skip
    rc = subprocess.run(cmd, cwd=ROOT).returncode
    log = ROOT / "build" / "harness" / out_name / "stdout_1.log"
    text = log.read_text(encoding="latin-1") if log.is_file() else ""
    loads = re.findall(r"afterLevelLoad level='(.*?)' path=", text)
    ok = rc == 0 and "[in] script quit" in text
    print(f"[{name}] clean scripted quit: {'PASS' if ok else 'FAIL'}")
    # the loads after the first gameplay level (Intro, Surdana) must be exactly the expected ones, in order
    seen = loads[2:]
    for i, level in enumerate(expect):
        hit = i < len(seen) and seen[i] == level
        ok &= hit
        print(f"[{name}] level change {i + 1} to {level!r}: {'PASS' if hit else 'FAIL'}  (loads seen: {seen})")
    fired = len(re.findall(r"\[levelchange\] firing exit", text))
    print(f"[{name}] exits fired: {fired}  {'PASS' if fired == len(expect) else 'FAIL'}")
    ok &= fired == len(expect)
    print(f"[{name}] output: build/harness/{out_name}")
    return ok


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--case", choices=sorted(CASES), action="append", help="run only this case (repeatable)")
    ap.add_argument("--steps", help="custom DRAKAN_LEVELCHANGE steps (runs as case 'custom', no level checks)")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC", help="play folder to copy DLLs / ini from")
    ap.add_argument("--prefix", default="lc", help="private folders build/<prefix>-play, build/harness/<prefix>-<case>")
    ap.add_argument("--frames", type=int, default=600, help="frames in a level before the next step fires")
    a = ap.parse_args()
    if not a.exe.is_file():
        raise SystemExit(f"{a.exe} not found (build ps2EntryRunner first)")
    results = {}
    if a.steps:
        results["custom"] = run_case(a, "custom", a.steps, [])
    else:
        for name in a.case or list(CASES):
            steps, expect = CASES[name]
            results[name] = run_case(a, name, steps, expect)
    for name, ok in results.items():
        print(("PASS " if ok else "FAIL ") + name)
    return 0 if all(results.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
