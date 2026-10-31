"""Candidate spawn points and pickups for a multiplayer map (configs/mp_maps.ini) from a decoded level.

  python -I analysis/scripts/lvl_objects.py --iso "<game>.iso" --level LEVEL1NA        # build/lvl-objects/LEVEL1NA.json
  python -I analysis/scripts/arena_candidates.py LEVEL1NA --center 466944 450560 --radius 9000
  python -I analysis/scripts/arena_candidates.py LEVEL1NA --center 466944 450560 --radius 9000 --spawns 8 --spacing 4000

Prints (x, y, z) of ground-hugging decor (flowers, grass sods: they sit on the terrain, so the y is a walkable height and
the spot is open ground, not inside a building) picked greedily so every pick is at least --spacing from the others, and
the level's loose items (weapons, potions, power ups) in the circle, with their resource ids, as ready-to-paste
`spawn =` / `pickup =` lines. Candidates are proposals: validate them in game (DRAKAN_ARENA_TOUR, docs/reference/netcode.md
"Multiplayer maps"). The output depends on game data, so it goes to the terminal, never into the repository.
"""

import argparse
import json
import math
import sys
from pathlib import Path

DECOMP_DIR = Path(__file__).resolve().parents[2]
GROUND_WORDS = ("Flower", "Grass (Sod)")
ITEM_CLASSES = {
    "0x1000e": "weapon",
    "0x1004f": "bow",
    "0x1000a": "potion",
    "0x10059": "potion",
    "0x100ab": "mana",
    "0x10039": "spell",
    "0x1002d": "armor",
    "0x10090": "shield",
}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("level", help="LEVEL1NA (build/lvl-objects/<level>.json) or a path to a json file")
    ap.add_argument("--center", nargs=2, type=float, metavar=("X", "Z"), required=True)
    ap.add_argument("--radius", type=float, default=9000.0)
    ap.add_argument("--spawns", type=int, default=8)
    ap.add_argument("--spacing", type=float, default=3500.0)
    ap.add_argument("--yaw", action="store_true", help="face every spawn towards the centre")
    a = ap.parse_args()

    path = Path(a.level)
    if not path.suffix:
        path = DECOMP_DIR / "build" / "lvl-objects" / f"{a.level}.json"
    data = json.loads(path.read_text(encoding="utf-8"))
    cx, cz = a.center

    def inside(o) -> bool:
        return math.hypot(o["pos"][0] - cx, o["pos"][2] - cz) <= a.radius

    ground = [o for o in data["objects"] if inside(o) and any(w in (o.get("name") or "") for w in GROUND_WORDS)]
    ground.sort(key=lambda o: -math.hypot(o["pos"][0] - cx, o["pos"][2] - cz))  # outer ring first
    picks: list[dict] = []
    for o in ground:
        if all(math.hypot(o["pos"][0] - p["pos"][0], o["pos"][2] - p["pos"][2]) >= a.spacing for p in picks):
            picks.append(o)
        if len(picks) >= a.spawns:
            break
    print(f"; {data.get('level_name')}: {len(ground)} ground decor objects in the circle, {len(picks)} picked")
    print(f"bounds = {cx - a.radius:.0f} {cz - a.radius:.0f} {cx + a.radius:.0f} {cz + a.radius:.0f}")
    for o in picks:
        x, y, z = o["pos"]
        yaw = math.degrees(math.atan2(cx - x, cz - z)) if a.yaw else 0
        print(f"spawn = {x:.0f} {y + 40:.0f} {z:.0f} {yaw:.0f}   ; {o['name']}")
    print("; loose items in the circle")
    for o in data["objects"]:
        cls = ITEM_CLASSES.get(o.get("runtime_class"))
        if cls and inside(o):
            x, y, z = o["pos"]
            print(f"pickup = {x:.0f} {y:.0f} {z:.0f} 30 name={o['name']}   ; {cls}, res {o.get('res_id')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
