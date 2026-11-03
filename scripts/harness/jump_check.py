"""Check native takeoff and travel from play_run.py --recipe scripts/harness/recipes/forward_jump.txt logs."""

from __future__ import annotations

import argparse
import json
import math
import re
from pathlib import Path

ROW = re.compile(
    r"\[in-pos\] f=\d+ pos=\(([-\d.]+) ([-\d.]+) ([-\d.]+)\).*?"
    r"st=([\da-f]+).*?spd=([-\d.]+) mv=([\da-f]+)/([\da-f]+)/([\da-f]+) jt=(-?\d+).*?dt=([\d.]+)"
)


def check(text: str) -> dict:
    rows = []
    for match in ROW.finditer(text):
        x, y, z, state, speed, forward, sideways, up, kind, dt = match.groups()
        rows.append(
            dict(pos=(float(x), float(y), float(z)), dt=float(dt), up=int(up, 16), kind=int(kind), state=int(state, 16))
        )
    edges = [i for i, row in enumerate(rows) if row["up"] & 1 and (i == 0 or not rows[i - 1]["up"] & 1)]
    checks = []
    for index, (trial, edge) in enumerate(zip(("run", "walk", "standing"), edges, strict=False)):
        base = rows[edge]["pos"]
        end = edges[index + 1] if index + 1 < len(edges) else len(rows)
        samples = []
        takeoff = []
        elapsed = 0.0
        for row in rows[edge + 1 : end]:
            samples.append(row)
            if elapsed < 0.12:
                takeoff.append(row)
            elapsed += row["dt"]
            if elapsed >= 1.5:
                break
        travel = max((math.hypot(r["pos"][0] - base[0], r["pos"][2] - base[2]) for r in samples), default=0)
        rise = max((r["pos"][1] - base[1] for r in samples), default=0)
        wanted = 1 if trial == "standing" else 2
        kinds = sorted({r["kind"] for r in samples})
        takeoff_travel = max((math.hypot(r["pos"][0] - base[0], r["pos"][2] - base[2]) for r in takeoff), default=0)
        # Native animation/collision settling can move a standing jump a few units (moving jumps travel ~2400).
        moving_jump = (
            takeoff_travel > 100 and any(r["kind"] == 2 for r in takeoff) and all(r["state"] != 19 for r in takeoff)
        )
        passed = wanted in kinds and rise > 10 and (travel < 20 if trial == "standing" else moving_jump)
        checks.append(
            dict(
                trial=trial,
                passed=passed,
                kinds=kinds,
                horizontal_travel=travel,
                takeoff_travel=takeoff_travel,
                rise=rise,
            )
        )
    passed = len(edges) == 3 and len(checks) == 3 and all(c["passed"] for c in checks) and "[in] script quit" in text
    return dict(passed=passed, jump_requests=len(edges), checks=checks)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    args = parser.parse_args()
    logs = sorted(args.directory.glob("stdout_*.log"), key=lambda p: p.stat().st_mtime)
    if not logs:
        parser.error("no stdout_*.log found")
    result = check(logs[-1].read_text(encoding="utf-8", errors="replace"))
    print(json.dumps(result, indent=2))
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
