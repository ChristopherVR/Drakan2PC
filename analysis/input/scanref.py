import sys

from elf import *

lo, hi = int(sys.argv[1], 16), int(sys.argv[2], 16)
TS = 0x100000
TE = 0x100000 + 0x3B0000
lui = {}
res = []
for va in range(0x100000, 0x4B8000, 4):
    w = u32(va)
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    imm = w & 0xFFFF
    if op == 0xF:
        lui[rt] = imm << 16
    elif op in (9, 0xD, 0x23, 0x2B, 0x21, 0x25, 0x20, 0x24, 0x31, 0x39, 0x1F) and rs in lui:
        s = imm - 0x10000 if imm & 0x8000 and op != 0xD else imm
        a = (lui[rs] + s) & 0xFFFFFFFF
        if lo <= a < hi:
            res.append((va, a))
        if op in (9, 0xD) and rt == rs:
            pass
    if op not in (0xF,):
        # kill reg on write (approx) for rt of immediate ops / rd of R-type
        if op == 0:
            rd = (w >> 11) & 31
            if rd in lui and (w & 0x3F) not in (8,):
                lui.pop(rd, None)
        elif op not in (0x2B, 0x28, 0x29, 0x2A, 0x2E, 0x39, 0x3D, 0x3F, 4, 5, 6, 7, 1, 2, 3, 0x2C, 0x2D):
            if op in (9, 0xD) and rs in lui and False:
                pass
            lui.pop(rt, None) if op != 0xF else None
for va, a in res:
    print(hex(va), hex(a))
