#!/usr/bin/env python
"""List callers (jal) of given function addresses, and j/jal targets stats.
python -I gfx_callers.py ELF ADDR..."""

import bisect
import struct
import sys

d = open(sys.argv[1], "rb").read()
TB, TOFF, TSZ = 0x100000, 0x1000, 0x3D73F4
W = struct.unpack("<%dI" % (TSZ // 4), d[TOFF : TOFF + TSZ // 4 * 4])
fs = [TB + 4 * i for i, x in enumerate(W) if (x >> 16) in (0x27BD, 0x67BD) and (x & 0x8000)]


def fn(a):
    i = bisect.bisect_right(fs, a) - 1
    return fs[i] if i >= 0 else 0


for t in sys.argv[2:]:
    t = int(t, 16)
    r = []
    for i, x in enumerate(W):
        if (x >> 26) in (2, 3) and ((x & 0x3FFFFFF) << 2) | 0 == (t & 0x0FFFFFFF):
            r.append(TB + 4 * i)
    print("%06x callers: %s" % (t, ", ".join("%06x(f%06x)" % (a, fn(a)) for a in r)))
