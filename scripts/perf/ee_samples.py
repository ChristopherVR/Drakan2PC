"""Resolve the EE-thread RIP samples of a DRAKAN_FRAME_AUDIT=3 run against the linker map and list what the thread ran
while the present loop produced long frames.

    DRAKAN_FRAME_AUDIT=3 python scripts/harness/play_run.py --name x --recipe scripts/harness/recipes/fps_steady_wall.txt
    python scripts/perf/ee_samples.py build/harness/x/drakan.log build/final-game/bin/drakan.map [--gap-ms 15] [--tail-s 15]

Log lines: `[ft] P <us> <serial>` present, `[ft] X <us> <rip rva> <first main-module caller rva> <image size>` sample of the EE
thread every ~0.5 ms (the thread is suspended for each sample, so absolute fps drops a little). Samples inside system DLLs
are attributed to their first caller in the game image (`EXT<-`).
"""

import argparse
import bisect
import collections
import itertools
import re
from pathlib import Path

SYMBOL = re.compile(r"^\s*[0-9a-f]{4}:[0-9a-f]{8}\s+(\S+)\s+([0-9a-f]{16})\s")


def load_symbols(map_path: Path) -> tuple[list[int], list[str], int]:
    base = 0x140000000
    symbols = []
    for line in map_path.read_text(errors="replace").splitlines():
        if "Preferred load address is" in line:
            base = int(line.split("is")[1].split()[0], 16)
        match = SYMBOL.match(line)
        if match:
            symbols.append((int(match.group(2), 16), match.group(1)))
    symbols.sort()
    return [a for a, _ in symbols], [n for _, n in symbols], base


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("log", type=Path)
    ap.add_argument("map", type=Path)
    ap.add_argument("--gap-ms", type=float, default=15.0, help="present gap that counts as a hitch")
    ap.add_argument("--tail-s", type=float, default=15.0, help="only the last N seconds of the log")
    ap.add_argument("--top", type=int, default=14)
    args = ap.parse_args()

    addrs, names, base = load_symbols(args.map)

    def resolve(rva: int) -> str:
        i = bisect.bisect(addrs, base + rva) - 1
        return f"{names[i][:70]}+{base + rva - addrs[i]:#x}" if i >= 0 else hex(rva)

    presents, samples = [], []
    for line in args.log.read_text(errors="replace").splitlines():
        parts = line.split()
        if len(parts) >= 3 and parts[0] == "[ft]":
            if parts[1] == "P":
                presents.append(int(parts[2]) / 1000.0)
            elif parts[1] == "X" and len(parts) >= 6:
                samples.append((int(parts[2]) / 1000.0, int(parts[3], 16), int(parts[4], 16), int(parts[5], 16)))
    if not presents:
        raise SystemExit("no [ft] P lines: run with DRAKAN_FRAME_AUDIT=2 or 3")
    start = presents[-1] - args.tail_s * 1000.0

    def where(rip: int, caller: int, size: int) -> str:
        return resolve(rip) if rip < size else "EXT<-" + resolve(caller)

    inside, overall, hitches = collections.Counter(), collections.Counter(), 0
    windows = [(a, b) for a, b in itertools.pairwise(presents) if a >= start and b - a > args.gap_ms]
    for t, rip, caller, size in samples:
        if t < start:
            continue
        overall[where(rip, caller, size)] += 1
    for a, b in windows:
        hitches += 1
        for t, rip, caller, size in samples:
            if a <= t <= b:
                inside[where(rip, caller, size)] += 1
    print(f"{hitches} present gaps over {args.gap_ms} ms in the last {args.tail_s} s; EE-thread samples inside them:")
    for name, count in inside.most_common(args.top):
        print(f"{count:6d}  {name}")
    print("\nall samples in the window:")
    for name, count in overall.most_common(args.top // 2):
        print(f"{count:6d}  {name}")


if __name__ == "__main__":
    main()
