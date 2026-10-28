#!/usr/bin/env python
"""Summarise the thread with most executable RIP samples (not CPU time).
Usage: hostprof_cat.py hostprof.csv drakan.map [topN]
"""

import bisect
import collections
import csv
import re
import sys

csvf, mapf = sys.argv[1], sys.argv[2]
top = int(sys.argv[3]) if len(sys.argv) > 3 else 25
rx = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})")
syms = []
base = None
for l in open(mapf, errors="replace"):
    if base is None and "Preferred load address is" in l:
        base = int(l.split("is")[1].split()[0], 16)
    m = rx.match(l)
    if m:
        syms.append((int(m.group(4), 16), m.group(3)))
syms = sorted(set(syms))
addrs = [a for a, _ in syms]


def res(rva):
    i = bisect.bisect_right(addrs, base + rva) - 1
    return syms[i][1] if i >= 0 else "?"


rows = list(csv.DictReader(open(csvf)))
cnt = collections.Counter()
for r in rows:
    if r["module"].lower().startswith("drakan"):
        cnt[r["tid"]] += int(r["count"])
busy = cnt.most_common(1)[0][0]
cat = collections.Counter()
fn = collections.Counter()
tot = 0
ext = collections.Counter()
for r in rows:
    if r["tid"] != busy:
        continue
    c = int(r["count"])
    tot += c
    if not r["module"].lower().startswith("drakan"):
        cat["dll:" + r["module"]] += c
        caller = int(r.get("caller") or "0", 16)
        ext[(r["module"], r["rva"], res(caller) if caller else "?")] += c
        continue
    n = res(int(r["rva"], 16))
    fn[n] += c
    if re.match(r"^\??(f_0x|entry_)", n):
        k = "generated EE code"
    elif "VU1Interpreter" in n:
        k = "VU1Interpreter"
    elif "@gsr@@" in n:
        k = "gsr (d3d11 GS)"
    elif "EeScheduler" in n:
        k = "EeScheduler"
    elif "PS2Runtime" in n:
        k = "PS2Runtime"
    elif "PS2Memory" in n:
        k = "PS2Memory"
    elif "fmv" in n:
        k = "fmv"
    elif "iop" in n:
        k = "iop"
    else:
        k = "other"
    cat[k] += c
print(f"thread with most executable RIP samples: {busy}, {tot} total wall-clock samples")
print("Samples include blocked time; percentages are not CPU consumption or frame-time shares.")
for k, v in cat.most_common(12):
    print(f"{v:7d} {100 * v / tot:5.1f}%  {k}")
print("-- top functions")
for k, v in fn.most_common(top):
    print(f"{v:7d} {100 * v / tot:5.1f}%  {k[:110]}")
print("-- system-DLL samples by (module, rva, first caller in the exe)")
for (m, rva, caller), v in ext.most_common(15):
    print(f"{v:7d} {100 * v / tot:5.1f}%  {m}+{rva}  <- {caller[:100]}")
