#!/usr/bin/env python
"""Histogram GS A+D register ids passed in a1 to display-driver setreg functions.
python -I gfx_adregs.py ELF TARGET_FUNC_ADDR..."""

import bisect
import collections
import struct
import sys

d = open(sys.argv[1], "rb").read()
TB, TOFF, TSZ = 0x100000, 0x1000, 0x3D73F4
W = struct.unpack("<%dI" % (TSZ // 4), d[TOFF : TOFF + TSZ // 4 * 4])
GS = {
    0: "PRIM",
    1: "RGBAQ",
    2: "ST",
    3: "UV",
    4: "XYZF2",
    5: "XYZ2",
    6: "TEX0_1",
    7: "TEX0_2",
    8: "CLAMP_1",
    9: "CLAMP_2",
    0xA: "FOG",
    0xC: "XYZF3",
    0xD: "XYZ3",
    0x14: "TEX1_1",
    0x15: "TEX1_2",
    0x16: "TEX2_1",
    0x17: "TEX2_2",
    0x18: "XYOFFSET_1",
    0x19: "XYOFFSET_2",
    0x1A: "PRMODECONT",
    0x1B: "PRMODE",
    0x1C: "TEXCLUT",
    0x22: "SCANMSK",
    0x34: "MIPTBP1_1",
    0x35: "MIPTBP1_2",
    0x36: "MIPTBP2_1",
    0x37: "MIPTBP2_2",
    0x3B: "TEXA",
    0x3D: "FOGCOL",
    0x3F: "TEXFLUSH",
    0x40: "SCISSOR_1",
    0x41: "SCISSOR_2",
    0x42: "ALPHA_1",
    0x43: "ALPHA_2",
    0x44: "DIMX",
    0x45: "DTHE",
    0x46: "COLCLAMP",
    0x47: "TEST_1",
    0x48: "TEST_2",
    0x49: "PABE",
    0x4A: "FBA_1",
    0x4B: "FBA_2",
    0x4C: "FRAME_1",
    0x4D: "FRAME_2",
    0x4E: "ZBUF_1",
    0x4F: "ZBUF_2",
    0x50: "BITBLTBUF",
    0x51: "TRXPOS",
    0x52: "TRXREG",
    0x53: "TRXDIR",
    0x54: "HWREG",
    0x60: "SIGNAL",
    0x61: "FINISH",
    0x62: "LABEL",
}
targets = [int(x, 16) for x in sys.argv[2:]]
fs = [TB + 4 * i for i, x in enumerate(W) if (x >> 16) in (0x27BD, 0x67BD) and (x & 0x8000)]


def fn(a):
    i = bisect.bisect_right(fs, a) - 1
    return fs[i] if i >= 0 else 0


res = collections.defaultdict(list)
for i, x in enumerate(W):
    if (x >> 26) == 3 and (((x & 0x3FFFFFF) << 2) in targets):
        a = TB + 4 * i
        reg = None
        for j in range(i + 1, i - 10, -1):  # include delay slot
            y = W[j]
            if (y >> 16) & 31 == 5 and (y >> 26) in (9, 0xD) and ((y >> 21) & 31) == 0:
                reg = y & 0xFFFF
                break
        res[reg].append(fn(a))
for r, l in sorted(res.items(), key=lambda t: (t[0] is None, t[0])):
    print(
        "%s %s: %d calls from %s"
        % (hex(r) if r is not None else "?", GS.get(r, ""), len(l), " ".join("%x" % f for f in sorted(set(l))[:10]))
    )
