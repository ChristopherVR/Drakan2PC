"""Link statistics of the level logic: which classes send which message value to which classes.

  python -I analysis/scripts/logic_graph.py            # reads build/lvl-objects/*.json (lvl_objects.py) and build/odb-fields/*.json (odb_fields.py --json)
  python -I analysis/scripts/logic_graph.py --source 0x1003d   # message x target table for one runtime class

The message value of an edge is the source's primary message field (docs/reference/level-format.md "Level logic"). Output only,
nothing is written; used to back the [disc] tags of the logic tables.
"""

import argparse
import collections
import glob
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PRIMARY = {  # runtime class -> (field name, class default)
    "0x1003d": ("Trigger Message", 5),
    "0x1003e": ("Trigger Message", 1),
    "0x10007": ("Message To Send", 1),
    "0x1000d": ("Message to send when on", 1),
    "0x100e0": ("Outgoing Message", 0),
    "0x10031": ("Message To Send", 8),
}


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument("--source", help="runtime class, e.g. 0x1003d")
    a = ap.parse_args()
    roles = collections.Counter()
    pair = collections.defaultdict(collections.Counter)
    total = dangling = 0
    for f in sorted(glob.glob(str(ROOT / "build" / "lvl-objects" / "*.json"))):
        d = json.load(open(f))
        by = {o["id"]: o for o in d["objects"]}
        for o in d["objects"]:
            for t in o["links"]:
                total += 1
                if t not in by:
                    dangling += 1
                    continue
                roles[(o["runtime_class"], by[t]["runtime_class"])] += 1
                if o["runtime_class"] in PRIMARY and (not a.source or a.source == o["runtime_class"]):
                    name, default = PRIMARY[o["runtime_class"]]
                    v = {p["name"]: p["value"] for p in o["params"]}.get(name, default)
                    pair[(o["runtime_class"], v)][by[t]["runtime_class"] + " " + by[t]["name"][:18]] += 1
    print(f"links {total}, dangling {dangling}")
    for (s, t), n in roles.most_common(25):
        print(f"{n:6} {s} -> {t}")
    for k in sorted(pair):
        print(k, pair[k].most_common(4))


if __name__ == "__main__":
    main()
