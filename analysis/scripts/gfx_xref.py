#!/usr/bin/env python
"""Find code references (lui+addiu/ori pairs, also lui+load/store offset) to addresses.
python -I gfx_xref.py ELF ADDR [ADDR...] (hex)"""

import bisect
import struct
import sys

ELF = sys.argv[1]
targets = [int(x, 16) for x in sys.argv[2:]]
d = open(ELF, "rb").read()
TB, TOFF, TSZ = 0x100000, 0x1000, 0x3D73F4
W = struct.unpack("<%dI" % (TSZ // 4), d[TOFF : TOFF + TSZ // 4 * 4])


def sx(v):
    return v - 0x10000 if v & 0x8000 else v


fs = [TB + 4 * i for i, x in enumerate(W) if (x >> 16) in (0x27BD, 0x67BD) and (x & 0x8000)]


def fn(a):
    i = bisect.bisect_right(fs, a) - 1
    return fs[i] if i >= 0 else 0


def refs(tset):
    out = {t: [] for t in tset}
    lui = {}  # reg -> (hi, index)
    for i, x in enumerate(W):
        op = x >> 26
        rs = (x >> 21) & 31
        rt = (x >> 16) & 31
        imm = x & 0xFFFF
        a = TB + 4 * i
        if op == 0x0F:
            lui[rt] = (imm << 16, i)
            continue
        if (
            op
            in (
                0x09,
                0x0D,
                0x20,
                0x21,
                0x23,
                0x24,
                0x25,
                0x28,
                0x29,
                0x2B,
                0x31,
                0x39,
                0x37,
                0x3F,
                0x1E,
                0x1F,
                0x35,
                0x3D,
                0x27,
            )
            and rs in lui
            and i - lui[rs][1] < 64
        ):
            v = (lui[rs][0] + sx(imm)) & 0xFFFFFFFF if op != 0x0D else (lui[rs][0] | imm)
            if v in out:
                out[v].append(a)
            if op in (0x09, 0x0D) and rt != rs:
                lui[rt] = (v, i)  # propagate full const
        # clobber
        if op in (0x23, 0x24, 0x25, 0x20, 0x21, 0x37, 0x1E) and rt in lui and rt != rs:
            lui.pop(rt, None)
    return out


if __name__ == "__main__":
    r = refs(targets)
    for t, l in r.items():
        print("%06x: %s" % (t, ", ".join("%06x(f%06x)" % (a, fn(a)) for a in l)))
