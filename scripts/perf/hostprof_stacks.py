#!/usr/bin/env python
"""Inclusive (cumulative) and self cost of the EE thread from hostprof_stacks.csv (PS2X_HOSTPROF + PS2X_HOSTPROF_STACKS=1).

Usage: hostprof_stacks.py hostprof_stacks.csv drakan.map [topN]

Each row is one distinct main-module call chain (leaf first, RVAs) and its sample count. A function's inclusive share is
the fraction of samples whose chain contains it (counted once per sample even when recursive); self is the leaf frame.
Runtime frames are also grouped into buckets (scheduler / checkpoint, dispatch, guest memory, our hooks, renderer
hand-off) so the overhead around the guest code can be read directly. The .map must be the one written with the exe
that produced the samples.
"""

import bisect
import collections
import csv
import re
import sys

BUCKETS = [
    (
        "checkpoint / scheduler / IOP time",
        re.compile(
            r"cpFlush|checkpointDue|accountCycles|advanceEeTimers|advanceIopEeCycles|hostVblankDue|thread@EeScheduler|IopTimrman|runCycles@Impl@IopEmulator|beginNextReady|servicePendingGuestCallbacks|runEeCycles"
        ),
    ),
    ("dispatch (JALR / branch lookup)", re.compile(r"dispatchGuestBranch|lookupFunction|hasFunction|traceTransfer")),
    (
        "guest memory",
        re.compile(
            r"PS2Memory|translateAddress|read8|read16|read32|read64|read128|write8|write16|write32|write64|write128|Ps2Fast"
        ),
    ),
    ("VIF / VU1 / DMA", re.compile(r"vif|Vif|VIF|vu1|Vu1|VU1|dmac|Dmac|DMAC")),
    ("our hooks (self only is meaningful)", re.compile(r"drakan_")),
    ("renderer hand-off", re.compile(r"GsrBridge|gsr@@|GS@@")),
]


def load_map(path):
    rx = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})")
    syms, base = [], None
    with open(path, errors="replace") as f:
        for line in f:
            if base is None and "Preferred load address is" in line:
                base = int(line.split("is")[1].split()[0], 16)
            m = rx.match(line)
            if m:
                syms.append((int(m.group(4), 16), m.group(3)))
    syms = sorted(set(syms))
    return base, [a for a, _ in syms], [n for _, n in syms]


def main():
    csv_path, map_path = sys.argv[1], sys.argv[2]
    top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
    base, addrs, names = load_map(map_path)
    cache = {}

    def resolve(rva):
        if rva not in cache:
            i = bisect.bisect_right(addrs, base + rva) - 1
            cache[rva] = names[i] if i >= 0 else "?"
        return cache[rva]

    total = 0
    inclusive, self_cost = collections.Counter(), collections.Counter()
    bucket_incl, bucket_self = collections.Counter(), collections.Counter()
    guest_self = 0
    with open(csv_path) as f:
        for row in csv.DictReader(f):
            count = int(row["count"])
            frames = [resolve(int(x, 16)) for x in row["frames"].split(";") if x]
            total += count
            if not frames:
                self_cost["(outside the exe)"] += count
                continue
            leaf = frames[0]
            self_cost[leaf] += count
            if leaf.startswith("f_0x") or leaf.startswith("?f_0x"):
                guest_self += count
            for name in set(frames):
                inclusive[name] += count
            seen = set()
            for label, rx in BUCKETS:
                if any(rx.search(n) for n in frames) and label not in seen:
                    seen.add(label)
                    bucket_incl[label] += count
                if rx.search(leaf):
                    bucket_self[label] += count
    if not total:
        print("no samples")
        return
    pct = lambda c: 100.0 * c / total
    print(f"{total} EE thread samples; guest generated code (f_0x*) self {pct(guest_self):.1f}%")
    print("\nbuckets (inclusive / self):")
    for label, _ in BUCKETS:
        print(f"  {pct(bucket_incl[label]):5.1f}% / {pct(bucket_self[label]):5.1f}%  {label}")
    print(f"\ntop {top} by inclusive:")
    for name, c in inclusive.most_common(top):
        print(f"  {pct(c):5.1f}%  self {pct(self_cost[name]):5.1f}%  {name}")
    print(f"\ntop {top} by self:")
    for name, c in self_cost.most_common(top):
        print(f"  {pct(c):5.1f}%  {name}")


if __name__ == "__main__":
    main()
