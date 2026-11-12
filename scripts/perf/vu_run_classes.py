"""Shares of the VU1 microprogram's stages in a recording of VU1 runs.

Input: the per-run CSV that the game's VU1 replay writes (docs/journal/2026-11-12/gpu-vertex-feasibility.md):

    DRAKAN_VU_BENCH=<recording.vur>,5 DRAKAN_VU_BENCH_RUNS=<runs.csv> <play>/drakan.exe
    python scripts/perf/vu_run_classes.py <runs.csv> [--by flags] [--top 30]

One run is one MSCAL or MSCNT. The stage of a run follows from the pc it starts at (and, for the indexed batch
path, from the batch header and the flag word), see STAGES below. Times are the best of the replay's repetitions per
run, single thread, so shares are of the VU1 engine's own time (no VIF unpack, no GIF hand-off).
"""

from __future__ import annotations

import argparse
import csv
from collections import defaultdict
from pathlib import Path

# Entry pc of a run -> stage name. The image has two MSCAL entries and resumes after each E bit.
ENTRY = {
    0x2A40: "ladder rung (nothing drawn)",
    0x2A50: "ladder rung (nothing drawn)",
    0x0000: "batch set-up",
}


def stage(row: dict) -> str:
    pc = int(row["entry_pc"], 16)
    out = int(row["exit_pc"], 16)
    flags = int(row["flags"], 16)
    if pc == 0x2A60:
        return "strips, constant colour" if flags & 8 else "strips, lit per vertex"
    if pc == 0x26E0:
        return "object set-up: " + ("drawn as strips" if out == 0x2A60 else "culled or sent to the batch path")
    if pc in ENTRY:
        return ENTRY[pc]
    if out == 0x1018:
        return "batch: primitive pass (index lists to strips)"
    if out == 0x1580:
        return "batch: primitive pass with the clipper"
    if out == 0x0E70:
        # one vertex pass per MSCNT, chosen by the flag word in the order the dispatcher tests the bits
        if flags & 0x80:
            return "batch: vertex pass, depth fade of alpha"
        if flags & 0x200:
            return "batch: vertex pass, texture matrix"
        if flags & 0x1000:
            return "batch: vertex pass, grid or point lights, then transform"
        if flags & 0x1:
            return "batch: vertex pass, light, transform, clip codes"
        return "batch: vertex pass, light and transform"
    return f"other (pc 0x{pc:04x} to 0x{out:04x})"


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("csv", type=Path)
    ap.add_argument("--by", choices=["stage", "flags", "pc"], default="stage")
    ap.add_argument("--top", type=int, default=40)
    a = ap.parse_args()

    rows = list(csv.DictReader(a.csv.open()))
    total_ns = sum(float(r["ns"]) for r in rows) or 1.0
    total_pairs = sum(int(r["pairs"]) for r in rows) or 1
    total_v = sum(int(r["vertices"]) for r in rows) or 1
    groups: dict[tuple, list[float]] = defaultdict(lambda: [0, 0.0, 0, 0, 0, 0])
    for r in rows:
        if a.by == "stage":
            key: tuple = (stage(r),)
        elif a.by == "flags":
            key = (stage(r), r["flags"])
        else:
            key = (r["entry_pc"], r["exit_pc"], r["flags"])
        g = groups[key]
        g[0] += 1
        g[1] += float(r["ns"])
        g[2] += int(r["pairs"])
        g[3] += int(r["vertices"])
        g[4] += int(r["fan_vertices"])
        g[5] += int(r["packets"])
    print(f"{len(rows)} runs, {total_ns * 1e-6:.2f} ms, {total_pairs} pairs, {total_v} vertices sent")
    print(f"{'runs':>7} {'time %':>7} {'pairs %':>8} {'vert %':>7} {'vertices':>9} {'fan':>7} {'packets':>8}  class")
    for key, g in sorted(groups.items(), key=lambda kv: -kv[1][1])[: a.top]:
        print(
            f"{g[0]:7d} {100 * g[1] / total_ns:7.1f} {100 * g[2] / total_pairs:8.1f} {100 * g[3] / total_v:7.1f} "
            f"{g[3]:9d} {g[4]:7d} {g[5]:8d}  {' '.join(key)}"
        )


if __name__ == "__main__":
    main()
