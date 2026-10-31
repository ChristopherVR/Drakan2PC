"""Engine lifecycle events in the running game (drakan_lifecycle, docs/journal/2026-10-31/ghost-spike-s3.md).

    python scripts/harness/lifecycle_run.py                         # all phases (about 6 game runs, 40-60 s each)
    python scripts/harness/lifecycle_run.py --phase restore         # one phase
    python scripts/harness/lifecycle_run.py --phase save_surdana --kill 1000a        # destroy a handle before a save
    python scripts/harness/lifecycle_run.py --phase restore --kill 100d3:restore     # ... before a restore (flushed)
    python scripts/harness/lifecycle_run.py --phase boot --kill 100d3:level          # ... before Level_Load (flushed)

Every phase runs scripts/harness/play_run.py (New Game on a private memory card under build/harness/<prefix>_<phase>/card,
never the player's saves) with DRAKAN_LIFECYCLE_LOG=1 and checks the `[lifecycle ...]` lines of stdout_1.log:

  boot          New Game: the intro and the first level are two Level_Loads (before/after pairs, ok, fromSave=0)
  save_surdana  quick save (F5) in Level 1A -> beforeSaveWrite / afterSaveWrite, the writer's own flush
  save_4a       New Game into Level 4A (start level redirect): Rynn (intro) -> Arokh is a mountChanged; quick save
  menu_save     ESC, Save Game, Enter on the first slot: the menu path fires the same two save events
  restore       F5, F8, F5, F8 in Surdana: quick save then quick load, twice -> beforeSaveRestore, beforeLevelLoad
                (fromSave), afterLevelLoad, afterSaveRestore in that order, and the saves fire the save events

--kill <class hex>[:save|restore|level] is the test aid of drakan_lifecycle.cpp (DRAKAN_LIFECYCLE_KILL): the first live
handle of that class is destroyed through 0x1413c0 in the chosen "before" event and the kill list / liveness are logged.
Not verifiable by script: Arokh -> Rynn (no level lands Arokh; see the journal).

The exe is dist/DrakanPC/drakan.exe (play_run.py default); --exe takes another play folder. Exit status 0 when every
check passed.
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAY = ROOT / "scripts" / "harness" / "play_run.py"
EXE: list[str] = []  # --exe passthrough

SAVE_F5 = """clock milliseconds
0 sync play 60
1000 key down F5
1700 key down F5
2400 key up F5
9000 quit
"""

MENU_SAVE = """clock milliseconds
0 sync play 60
500 key tap ESC
1800 key tap DOWN
2600 key tap ENTER
4200 key tap ENTER
8000 quit
"""

SAVE_LOAD_SAVE_LOAD = """clock milliseconds
0 sync play 60
500 key down F5
1300 key up F5
8000 key down F8
8700 key up F8
17000 shot loaded
18000 key down F5
18800 key up F5
26000 key down F8
26700 key up F8
35000 shot reloaded
36000 quit
"""

BOOT = "clock milliseconds\n0 sync play 60\n3000 quit\n"

LINE = re.compile(
    r"\[lifecycle ([0-9.]+)\] (\w+) level='([^']*)' path='([^']*)' fromSave=(\d) ok=(\d) result=(-?\d+) "
    r"mount=(\d)->(\d) flushes=(\d+)"
)


def run(prefix: str, phase: str, recipe: str, extra: list[str]) -> tuple[Path, list[dict]]:
    name = f"{prefix}_{phase}"
    out = ROOT / "build" / "harness" / name
    recipes = ROOT / "build" / "harness" / "lifecycle_recipes"
    recipes.mkdir(parents=True, exist_ok=True)
    path = recipes / f"{name}.txt"
    path.write_text(recipe, encoding="utf-8")
    cmd = [sys.executable, str(PLAY), "--name", name, "--recipe", str(path), "--env", "DRAKAN_LIFECYCLE_LOG=1"]
    cmd += [*EXE, *extra]
    print(f"== {phase}: {' '.join(cmd[2:])}", flush=True)
    proc = subprocess.Popen(cmd, cwd=ROOT)
    proc.wait()
    log = out / "stdout_1.log"
    text = log.read_text(encoding="latin-1") if log.is_file() else ""
    events = [
        dict(
            t=float(m[1]),
            kind=m[2],
            level=m[3],
            path=m[4],
            fromSave=m[5] == "1",
            ok=m[6] == "1",
            result=int(m[7]),
            prev=int(m[8]),
            cur=int(m[9]),
            flushes=int(m[10]),
        )
        for m in LINE.finditer(text)
    ]
    for ev in events:
        print(
            f"   {ev['t']:8.3f} {ev['kind']:18s} level={ev['level']!r} fromSave={ev['fromSave']} ok={ev['ok']} "
            f"mount={ev['prev']}->{ev['cur']} flushes={ev['flushes']}"
        )
    for line in text.splitlines():
        if line.startswith(("[lifecycle-test]", "[lifecycle] hooks")):
            print("   " + line)
    return out, events


def kinds(events: list[dict]) -> list[str]:
    return [e["kind"] for e in events]


def check(label: str, cond: bool, results: list[tuple[str, bool]]) -> None:
    print(f"   [{'PASS' if cond else 'FAIL'}] {label}")
    results.append((label, cond))


def saved(events: list[dict]) -> bool:
    return "beforeSaveWrite" in kinds(events) and any(e["kind"] == "afterSaveWrite" and e["ok"] for e in events)


def pairs(events: list[dict], before: str, after: str) -> list[tuple[dict, dict]]:
    return list(
        zip([e for e in events if e["kind"] == before], [e for e in events if e["kind"] == after], strict=False)
    )


def main() -> int:
    phases = ["boot", "save_surdana", "save_4a", "menu_save", "restore"]
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--phase", choices=phases)
    ap.add_argument("--prefix", default="lifecycle")
    ap.add_argument("--exe", help="game exe (play_run.py default: dist/DrakanPC/drakan.exe)")
    ap.add_argument("--kill", help="<class hex>[:save|restore|level] test aid: destroy a handle in a before event")
    a = ap.parse_args()
    if a.exe:
        EXE[:] = ["--exe", a.exe]

    def want(p: str) -> bool:
        return a.phase in (None, p)

    results: list[tuple[str, bool]] = []
    kill = ["--env", f"DRAKAN_LIFECYCLE_KILL={a.kill}"] if a.kill else []
    sur_card = ROOT / "build" / "harness" / f"{a.prefix}_save_surdana" / "card" / "mc0"

    if want("boot"):
        _, ev = run(a.prefix, "boot", BOOT, kill)
        loads = [e for e in ev if e["kind"] == "afterLevelLoad"]
        k = kinds(ev)
        check("two level loads at New Game, ok", len(loads) >= 2 and all(e["ok"] for e in loads), results)
        check(
            "every beforeLevelLoad has its afterLevelLoad",
            k.count("beforeLevelLoad") == k.count("afterLevelLoad") >= 2,
            results,
        )
        check("the level name of the last load is Surdana", bool(loads) and "Surdana" in loads[-1]["level"], results)
        check(
            "no kill-list flush inside a level load",
            all(b["flushes"] == f["flushes"] for b, f in pairs(ev, "beforeLevelLoad", "afterLevelLoad")),
            results,
        )

    if want("save_surdana"):
        _, ev = run(a.prefix, "save_surdana", SAVE_F5, kill)
        check("quick save: beforeSaveWrite, afterSaveWrite(ok)", saved(ev), results)
        check("quick save wrote Slot33", any(sur_card.glob("*Slot33/*.rsg")), results)
        check(
            "the writer flushes the kill list itself",
            any(f["flushes"] > b["flushes"] for b, f in pairs(ev, "beforeSaveWrite", "afterSaveWrite")),
            results,
        )

    if want("save_4a"):
        _, ev = run(a.prefix, "save_4a", SAVE_F5, ["--level", "4a", *kill])
        check("quick save in 4A fires both save events", saved(ev), results)
        check(
            "mountChanged Rynn -> Arokh (1 -> 2) when 4A loads after the intro",
            any(e["kind"] == "mountChanged" and (e["prev"], e["cur"]) == (1, 2) for e in ev),
            results,
        )

    if want("menu_save"):
        out, ev = run(a.prefix, "menu_save", MENU_SAVE, kill)
        check("menu save fires beforeSaveWrite / afterSaveWrite(ok)", saved(ev), results)
        check("menu save wrote Slot1/*.rsg", any((out / "card" / "mc0").glob("*Slot1/*.rsg")), results)

    if want("restore"):
        _, ev = run(a.prefix, "restore", SAVE_LOAD_SAVE_LOAD, kill)
        k = kinds(ev)
        order = ["beforeSaveRestore", "beforeLevelLoad", "afterLevelLoad", "afterSaveRestore"]
        i = k.index("beforeSaveRestore") if "beforeSaveRestore" in k else len(k)
        check(
            "restore order: beforeSaveRestore, beforeLevelLoad, afterLevelLoad, afterSaveRestore",
            [x for x in k[i:] if x in order][:4] == order,
            results,
        )
        check(
            "the level load inside a restore is flagged fromSave",
            any(e["kind"] == "beforeLevelLoad" and e["fromSave"] for e in ev),
            results,
        )
        check(
            "the second quick load fires the restore events again",
            k.count("beforeSaveRestore") >= 2 and k.count("afterSaveRestore") >= 2,
            results,
        )
        check(
            "both quick saves fire the save events",
            k.count("beforeSaveWrite") >= 2 and k.count("afterSaveWrite") >= 2,
            results,
        )

    failed = [label for label, ok in results if not ok]
    print(f"\n{len(results) - len(failed)}/{len(results)} checks passed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
