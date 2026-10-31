"""One game instance in a multiplayer map (drakan_arena, docs/reference/netcode.md "Multiplayer maps"): the arena world
mode without a network session (DRAKAN_ARENA_FORCE), for authoring and checking maps.

    python scripts/harness/arena_tour.py --map surdana-village                  # tour of every spawn point + world dump
    python scripts/harness/arena_tour.py --map surdana-village --tour 0 --walk  # no tour, walk forward and take a shot

New Game loads the map's base level straight in place of the intro level (the file redirect of the multiplayer launch, no
movie), the arena removes the campaign, `--tour N` visits each spawn point for N boundaries (about N / 60 s) and logs where
the player stands afterwards ("[arena] tour: spawn i: player now at ..."): a spawn point that is inside geometry or over a
drop shows there and on the screenshots. Two world dumps (DRAKAN_ARENA_DUMP: arena_before.json when
the level came up, arena_after.json once the campaign was removed) are written to dev/world of the private play folder
build/arena-<name> and summarised. Output build/harness/<name>/ (stdout_1.log, recipe.txt, *.png).
Needs a built game (build/final-game/bin/drakan.exe) and a play folder (dist/DrakanPC) for the DLLs.
"""

from __future__ import annotations

import argparse
import collections
import json
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import game_env, prepare_folder

ROOT = Path(__file__).resolve().parents[2]


def find_map(name: str, maps_file: Path | None = None) -> dict[str, str]:
    """The [arena <id>] section of configs/mp_maps.ini (or `maps_file`) whose id or name matches: {id, name, level}."""
    text = (maps_file or ROOT / "configs" / "mp_maps.ini").read_text(encoding="utf-8")
    cur: dict[str, str] = {}
    found = []
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        if line.startswith("[arena"):
            cur = {"id": line[6:].strip(" ]")}
            found.append(cur)
        elif "=" in line and cur:
            k, v = line.split("=", 1)
            cur.setdefault(k.strip().lower(), v.strip())
    for c in found:
        if name.lower() in (c.get("id", "").lower(), c.get("name", "").lower()):
            return c
    raise SystemExit(f"no map '{name}' in configs/mp_maps.ini")


def summarize(dump: Path) -> collections.Counter:
    """Counts per (type, class) of a world dump, plus "creatures" (hpKind creature) and "items" (types 10 / 11)."""
    d = json.loads(dump.read_text(encoding="utf-8"))
    c: collections.Counter = collections.Counter()
    for e in d.get("entities", []):
        c[(e.get("type"), e.get("class"))] += 1
        if e.get("hpKind") == "creature":
            c["creatures"] += 1
        if e.get("type") in (10, 11):
            c["items"] += 1
    c["entities"] = len(d.get("entities", []))
    return c


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", required=True)
    ap.add_argument("--name", default="")
    ap.add_argument("--tour", type=int, default=90, help="boundaries per spawn point (0 = no tour)")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--shots", type=int, default=0, help="screenshot every N ms of the tour (default: one per stop)")
    ap.add_argument("--walk", action="store_true")
    ap.add_argument("--wait", type=int, default=0, help="keep playing until this many ms after the level started")
    ap.add_argument("--shot-at", type=int, action="append", default=[], help="a screenshot at this many ms")
    ap.add_argument("--env", action="append", default=[], help="extra KEY=VALUE for the game (test aids)")
    a = ap.parse_args()

    m = find_map(a.map)
    name = a.name or ("arena_" + m["id"])
    out = ROOT / "build" / "harness" / name / "a"
    out.mkdir(parents=True, exist_ok=True)
    folder = ROOT / "build" / ("arena-" + name)
    exe = prepare_folder(a.src, a.exe, folder)

    # The module takes the tour screenshots itself (tour_<round>_<spawn>.png, halfway through each stop); the input script
    # only waits for the tour and takes the start / walk shots.
    lines = ["clock milliseconds", "0 sync play 60", "2500 shot s_start"]
    t = 4000 + (
        int(a.tour * 2 * 6 * 1000 / 100) if a.tour else 0
    )  # generous: a stop of N boundaries takes N / 100 s or less
    if a.walk:
        lines += [f"{t} key down W", f"{t + 3000} key up W", f"{t + 3300} shot s_walk"]
        t += 4000
    t = max(t, a.wait)
    lines += [f"{ms} shot extra_{ms}" for ms in sorted(a.shot_at)]
    lines += [f"{t + 2000} quit"]
    rec = out / "recipe.txt"
    rec.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")

    extra = {
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_ARENA_DUMP": "1",  # arena_before.json / arena_after.json in <folder>/dev/world
    }
    extra["DRAKAN_ARENA_FORCE"] = m["id"]
    for kv in a.env:
        k, _, v = kv.partition("=")
        extra[k] = v
    if a.tour:
        extra["DRAKAN_ARENA_TOUR"] = str(a.tour)
    env = game_env(out, folder, rec, a.timeout, extra)
    env.pop("DRAKAN_GHOST", None)
    start = time.time()
    with (out / "stdout_1.log").open("wb") as f:
        proc = subprocess.run([str(exe)], cwd=folder, env=env, stdout=f, stderr=subprocess.STDOUT)
    print(f"exit {proc.returncode} after {time.time() - start:.0f} s; output {out}")
    text = (out / "stdout_1.log").read_bytes().decode("latin-1")
    for line in text.splitlines():
        if "[arena]" in line:
            print(line.strip())
    for tag in ("before", "after"):
        d = folder / "dev" / "world" / f"arena_{tag}.json"
        if d.is_file():
            c = summarize(d)
            print(f"dump {tag}: {c['entities']} entities, {c['creatures']} creatures with HP, {c['items']} loose items")
    shots = sorted(out.glob("tour_*.png"))
    if shots:
        from PIL import Image, ImageDraw

        ims = [Image.open(p).convert("RGB").resize((320, 240)) for p in shots]
        cols = 4
        sheet = Image.new("RGB", (cols * 320, ((len(ims) + cols - 1) // cols) * 240))
        for i, (im, p) in enumerate(zip(ims, shots)):
            ImageDraw.Draw(im).text((6, 224), p.stem, fill=(255, 255, 0))
            sheet.paste(im, ((i % cols) * 320, (i // cols) * 240))
        sheet.save(out / "tour_sheet.png")
        print(f"tour sheet: {out / 'tour_sheet.png'} ({len(shots)} spawn points)")
    return 0 if proc.returncode == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
