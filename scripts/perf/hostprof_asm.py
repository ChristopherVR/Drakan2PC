#!/usr/bin/env python
"""Annotated disassembly of one function with per-instruction sample counts.
Usage: hostprof_asm.py hostprof.csv drakan.map drakan.exe <function-name-substring> [minPercent]"""

import bisect
import collections
import csv
import re
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_64, Cs

csvf, mapf, exef, pat = sys.argv[1:5]
minpct = float(sys.argv[5]) if len(sys.argv) > 5 else 0.5
rx = re.compile(r"^\s*([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+(\S+)\s+([0-9a-fA-F]{16})")
syms = []
base = None
for l in open(mapf, errors="replace"):
    if base is None and "Preferred load address is" in l:
        base = int(l.split("is")[1].split()[0], 16)
    m = rx.match(l)
    if m:
        syms.append((int(m.group(4), 16) - 0, m.group(3)))
syms = sorted(set(syms))
addrs = [a for a, _ in syms]
cand = [(a, n) for a, n in syms if pat in n]
if not cand:
    sys.exit("no symbol matches " + pat)
start, name = cand[0]
i = bisect.bisect_right(addrs, start)
end = addrs[i] if i < len(addrs) else start + 4096
# PE sections
d = open(exef, "rb").read()
pe = struct.unpack_from("<I", d, 0x3C)[0]
nsec = struct.unpack_from("<H", d, pe + 6)[0]
optsz = struct.unpack_from("<H", d, pe + 20)[0]
secoff = pe + 24 + optsz
secs = []
for k in range(nsec):
    o = secoff + 40 * k
    vsz, va, rsz, raw = struct.unpack_from("<IIII", d, o + 8)
    secs.append((va, max(vsz, rsz), raw))


def rva2off(rva):
    for va, sz, raw in secs:
        if va <= rva < va + sz:
            return raw + rva - va


rvs, rve = start - base, end - base
code = d[rva2off(rvs) : rva2off(rvs) + (rve - rvs)]
hist = collections.Counter()
tot = 0
for r in csv.DictReader(open(csvf)):
    if not r["module"].lower().startswith("drakan"):
        continue
    rva = int(r["rva"], 16)
    if rvs <= rva < rve:
        hist[rva] += int(r["count"])
    tot += int(r["count"])
fn_total = sum(hist.values())
print(
    f"{name}: {fn_total} samples in function ({100 * fn_total / max(tot, 1):.1f}% of all drakan-module samples), size {rve - rvs} bytes"
)
md = Cs(CS_ARCH_X86, CS_MODE_64)
for ins in md.disasm(code, rvs):
    c = hist.get(ins.address, 0)
    # attribute samples that land inside an instruction's span to it
    pct = 100 * c / max(fn_total, 1)
    mark = f"{c:6d} {pct:5.1f}%" if c else " " * 13
    if pct >= minpct or len(sys.argv) > 6:
        print(f"{mark}  {ins.address - rvs:5x}: {ins.mnemonic:8s} {ins.op_str}")
