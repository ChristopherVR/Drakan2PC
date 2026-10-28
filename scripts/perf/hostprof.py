#!/usr/bin/env python
"""Resolve hostprof.csv (PS2X_HOSTPROF) against the linker .map. Usage: hostprof.py hostprof.csv drakan.map [topN]"""

import bisect
import collections
import re
import sys

csv, mapf = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 40
syms = []
base = None
rx = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})")
with open(mapf, errors="replace") as f:
    for line in f:
        if base is None and "Preferred load address is" in line:
            base = int(line.split("is")[1].strip().split()[0], 16)
        m = rx.match(line)
        if m:
            syms.append((int(m.group(4), 16), m.group(3)))
syms = sorted(set(syms))
addrs = [a for a, _ in syms]
print(f"{len(syms)} map symbols, base {base:x}")


def resolve(rva):
    va = base + rva
    i = bisect.bisect_right(addrs, va) - 1
    return syms[i][1] if i >= 0 else "?"


per_tid = collections.Counter()
per_fn = collections.defaultdict(collections.Counter)
per_mod = collections.defaultdict(collections.Counter)
import csv as csvm

with open(csv) as f:
    for r in csvm.DictReader(f):
        tid, mod, rva, c = int(r["tid"]), r["module"], int(r["rva"], 16), int(r["count"])
        per_tid[tid] += c
        if mod.lower().startswith("drakan"):
            per_fn[tid][resolve(rva)] += c
        else:
            per_mod[tid][mod] += c
total = sum(per_tid.values())
for tid, n in per_tid.most_common():
    print(f"\n== thread {tid}: {n} samples ({100 * n / total:.1f}% of all)")
    mods = per_mod[tid]
    if mods:
        print("   other modules:", ", ".join(f"{m}:{c}" for m, c in mods.most_common(6)))
    fns = per_fn[tid]
    if fns:
        for fn, c in fns.most_common(top):
            print(f"   {c:7d} {100 * c / n:5.1f}%  {fn}")
