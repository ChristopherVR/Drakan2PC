"""Compare World JSON dumps (DRAKAN_WORLD=2 DRAKAN_WORLD_DUMP_FRAME=n,m,... [DRAKAN_WORLD_DUMP_LEVEL=<substring>]) and
print the entities whose position moved between the first and any later dump: proof of a door, gate or mover reacting.

    python scripts/harness/world_diff.py <dump dir> <level substring> [--last N] [--res 0x80931,0xb0943] [--min 50]

Only entities present in every dump are compared (matched by handle). Distances are game units. The player and entities whose
resource id is not in --res (when given) are skipped; creatures move on their own, so name the resource ids you expect to move.
"""

from __future__ import annotations

import argparse
import json
import math
import os
from pathlib import Path


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("dir", type=Path)
    ap.add_argument("level", help="substring of the dump file name (the level name, non alphanumerics are '_')")
    ap.add_argument("--last", type=int, default=5, help="newest N dumps to compare")
    ap.add_argument(
        "--res", default="", help="comma separated resource ids to report (default: every non-player entity)"
    )
    ap.add_argument("--min", type=float, default=50.0, help="smallest movement to report")
    a = ap.parse_args()
    files = sorted((f for f in a.dir.glob("world_*.json") if a.level in f.name), key=os.path.getmtime)[-a.last :]
    if len(files) < 2:
        print(f"need at least 2 dumps for '{a.level}' in {a.dir}, found {len(files)}")
        return 2
    dumps = [json.loads(f.read_text(encoding="utf-8")) for f in files]
    maps = [{e["handle"]: e for e in d["entities"]} for d in dumps]
    wanted = {r.lower() for r in a.res.split(",") if r}
    moved = 0
    for h, e in maps[0].items():
        if e.get("hpKind") == "player" or (wanted and e["resId"].lower() not in wanted):
            continue
        seq = [m.get(h) for m in maps]
        if any(s is None for s in seq):
            continue
        d = max(math.dist(seq[0]["pos"], s["pos"]) for s in seq)
        if d >= a.min:
            moved += 1
            print(h, e["class"], e["resId"], f"moved {d:.0f}", [[round(x) for x in s["pos"]] for s in seq])
    print(f"{len(files)} dumps ({files[0].name} .. {files[-1].name}): {moved} entities moved >= {a.min:g}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
