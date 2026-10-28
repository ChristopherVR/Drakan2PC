#!/usr/bin/env python
"""Summarise functions in an address range: size, callees, VIF-ish immediates.
python -I gfx_funcsum.py ELF START END"""

import struct
import sys

d = open(sys.argv[1], "rb").read()
s0 = int(sys.argv[2], 16)
s1 = int(sys.argv[3], 16)
TB, TOFF, TSZ = 0x100000, 0x1000, 0x3D73F4
W = struct.unpack("<%dI" % (TSZ // 4), d[TOFF : TOFF + TSZ // 4 * 4])
fs = [TB + 4 * i for i, x in enumerate(W) if (x >> 16) in (0x27BD, 0x67BD) and (x & 0x8000)]
VIF = {
    0x14: "MSCAL",
    0x15: "MSCALF",
    0x17: "MSCNT",
    0x4A: "MPG",
    0x50: "DIRECT",
    0x51: "DIRECTHL",
    0x11: "FLUSH",
    0x13: "FLUSHA",
    0x06: "MSKPATH3",
}
for k, f in enumerate(fs):
    if f < s0 or f >= s1:
        continue
    e = fs[k + 1] if k + 1 < len(fs) else f + 4
    callees = []
    tags = set()
    spr = False
    gs = set()
    for a in range(f, e, 4):
        x = W[(a - TB) >> 2]
        if x >> 26 == 3:
            callees.append(((x & 0x3FFFFFF) << 2) | 0)
        if x >> 26 == 0x0F:
            imm = x & 0xFFFF
            if imm in [c << 8 for c in VIF]:
                tags.add(VIF[imm >> 8])
            if imm == 0x7000:
                spr = True
            if imm == 0x1100:
                tags.add("VUMEM")
        if x >> 26 in (0x0D, 0x09) and ((x >> 21) & 31) == 0:
            imm = x & 0xFFFF
            # AD register addresses commonly loaded as small imms: skip
    cs = sorted(set(callees))
    print(
        "%06x size %5d spr=%d tags=%s callees=%s"
        % (f, e - f, spr, ",".join(sorted(tags)), " ".join("%x" % c for c in cs[:14]))
    )
