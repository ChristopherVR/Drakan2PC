#!/usr/bin/env python
"""Scan Drakan EE .text for hardware register / VIF / GIF usage.
Usage: python -I gfx_scan.py <ELF> [section]"""

import collections
import struct
import sys

ELF = sys.argv[1]
d = open(ELF, "rb").read()
TB, TOFF, TSZ = 0x100000, 0x1000, 0x3D73F4
text = d[TOFF : TOFF + TSZ]
W = struct.unpack("<%dI" % (len(text) // 4), text[: len(text) // 4 * 4])


def w(a):
    return W[(a - TB) >> 2]


def sx16(v):
    return v - 0x10000 if v & 0x8000 else v


REG = [
    "zero",
    "at",
    "v0",
    "v1",
    "a0",
    "a1",
    "a2",
    "a3",
    "t0",
    "t1",
    "t2",
    "t3",
    "t4",
    "t5",
    "t6",
    "t7",
    "s0",
    "s1",
    "s2",
    "s3",
    "s4",
    "s5",
    "s6",
    "s7",
    "t8",
    "t9",
    "k0",
    "k1",
    "gp",
    "sp",
    "fp",
    "ra",
]

# function starts: addiu sp,sp,-N  (0x27bdxxxx with negative)  (or daddiu 0x67bd)
fstarts = [TB + 4 * i for i, x in enumerate(W) if (x >> 16) in (0x27BD, 0x67BD) and (x & 0x8000)]
import bisect


def func_of(a):
    i = bisect.bisect_right(fstarts, a) - 1
    return fstarts[i] if i >= 0 else 0


# simple const tracking: per basic region linear, reset at branch targets approx (ignore) ; track regs set by lui + ori/addiu
hits = collections.defaultdict(list)  # key -> list of (addr, kind, val)


def classify(v):
    if 0x10000000 <= v < 0x10010000:
        return "EEHW"
    if 0x11000000 <= v < 0x11010000:
        return "VUMEM"
    if 0x12000000 <= v < 0x12002000:
        return "GSPRIV"
    if 0x1A000000 <= v < 0x1A010000:
        return "IOP/SIF?"
    if 0x70000000 <= v < 0x70004000:
        return "SPR"
    return None


regs = {}
for i, x in enumerate(W):
    a = TB + 4 * i
    op = x >> 26
    rs = (x >> 21) & 31
    rt = (x >> 16) & 31
    imm = x & 0xFFFF
    if op == 0x0F:  # lui
        regs[rt] = imm << 16
        c = classify(imm << 16)
        if c:
            hits[c].append((a, "lui", imm << 16, rt))
    elif op in (0x0D, 0x09) and rs in regs and (rs == rt or True):  # ori / addiu
        v = (regs[rs] | imm) if op == 0x0D else (regs[rs] + sx16(imm))
        v &= 0xFFFFFFFF
        regs[rt] = v
        c = classify(v)
        if c:
            hits[c].append((a, "const", v, rt))
    elif op == 0x0F:
        pass
    else:
        # writes to a reg invalidate
        if op in (0x08, 0x09, 0x0C, 0x0E, 0x0A, 0x0B, 0x20, 0x21, 0x23, 0x24, 0x25, 0x37, 0x19, 0x18) and rt in regs:
            regs.pop(rt, None)
        if op == 0 and (x & 0x3F) not in (8, 0xC, 0xD, 0x18, 0x19, 0x1A, 0x1B):
            regs.pop((x >> 11) & 31, None)
        if op in (0x1, 0x2, 0x3, 0x4, 0x5, 0x6, 0x7):
            regs.clear() if False else None
# stores via lui-held base
out = collections.defaultdict(list)
for c, l in hits.items():
    for a, k, v, rt in l:
        out[(c, v & 0xFFFFF000 if k == "lui" else v, func_of(a))].append((a, k))
if __name__ == "__main__":
    for (c, v, f), l in sorted(out.items(), key=lambda t: (t[0][0], t[0][1], t[0][2])):
        print("%-7s %08x func %06x  at %s" % (c, v, f, ",".join("%06x" % a for a, k in l[:6])))
