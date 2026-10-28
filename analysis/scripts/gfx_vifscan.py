#!/usr/bin/env python
"""Find immediate constants in .text that look like VIF codes / DMA tags / GIF tags.
python -I gfx_vifscan.py ELF"""

import bisect
import collections
import struct
import sys

ELF = sys.argv[1]
d = open(ELF, "rb").read()
TB, TOFF, TSZ = 0x100000, 0x1000, 0x3D73F4
W = struct.unpack("<%dI" % (TSZ // 4), d[TOFF : TOFF + TSZ // 4 * 4])


def sx(v):
    return v - 0x10000 if v & 0x8000 else v


fs = [TB + 4 * i for i, x in enumerate(W) if (x >> 16) in (0x27BD, 0x67BD) and (x & 0x8000)]


def fn(a):
    i = bisect.bisect_right(fs, a) - 1
    return fs[i] if i >= 0 else 0


NAMES = {
    0x00: "NOP",
    0x01: "STCYCL",
    0x02: "OFFSET",
    0x03: "BASE",
    0x04: "ITOP",
    0x05: "STMOD",
    0x06: "MSKPATH3",
    0x07: "MARK",
    0x10: "FLUSHE",
    0x11: "FLUSH",
    0x13: "FLUSHA",
    0x14: "MSCAL",
    0x15: "MSCALF",
    0x17: "MSCNT",
    0x20: "STMASK",
    0x30: "STROW",
    0x31: "STCOL",
    0x4A: "MPG",
    0x50: "DIRECT",
    0x51: "DIRECTHL",
}
for c in range(0x60, 0x80):
    NAMES[c] = "UNPACK%02x" % c
res = collections.defaultdict(list)
cur = {}
for i, x in enumerate(W):
    a = TB + 4 * i
    op = x >> 26
    rs = (x >> 21) & 31
    rt = (x >> 16) & 31
    imm = x & 0xFFFF
    if op == 0x0F:
        cur[rt] = (imm << 16, i)
        hi = imm >> 8
        if (hi in NAMES) and (imm & 0x8000) == 0:
            # lui with imm >= 0x0200 cannot be a normal 32MB RAM pointer
            if imm >= 0x0200:
                res[(hi,)].append((a, imm << 16))
    elif op in (0x0D, 0x09) and rs in cur and i - cur[rs][1] < 6:
        v = (cur[rs][0] | imm) if op == 0x0D else (cur[rs][0] + sx(imm))
        v &= 0xFFFFFFFF
        cur[rt] = (v, i)
        hi = v >> 24
        if (
            hi in NAMES
            and v >= 0x02000000
            and not (0x10000000 <= v < 0x13000000)
            and not (0x70000000 <= v < 0x70004000)
        ):
            res[(hi,)].append((a, v))
for (hi,), l in sorted(res.items()):
    print("== %02x %s: %d hits" % (hi, NAMES[hi], len(l)))
    seen = collections.OrderedDict()
    for a, v in l:
        seen.setdefault(fn(a), []).append((a, v))
    for f, ll in list(seen.items())[:40]:
        print("   f%06x: %s" % (f, " ".join("%06x=%08x" % (a, v) for a, v in ll[:5])))
