#!/usr/bin/env python
"""Quick R5900 disassembly helper (capstone + lq/sq/COP2/MMI fallbacks).
python -I gfx_dis.py ELF START [COUNT|-eEND]"""

import struct
import sys

import capstone as c

ELF = sys.argv[1]
start = int(sys.argv[2], 16)
cnt = 64
if len(sys.argv) > 3:
    cnt = (int(sys.argv[3][2:], 16) - start) // 4 if sys.argv[3].startswith("-e") else int(sys.argv[3], 0)
d = open(ELF, "rb").read()
TB, TOFF = 0x100000, 0x1000
md = c.Cs(c.CS_ARCH_MIPS, c.CS_MODE_MIPS64 + c.CS_MODE_LITTLE_ENDIAN)
R = [
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


def sx(v):
    return v - 0x10000 if v & 0x8000 else v


def dis(a):
    o = TOFF + (a - TB)
    w = struct.unpack_from("<I", d, o)[0]
    op = w >> 26
    rs = (w >> 21) & 31
    rt = (w >> 16) & 31
    imm = w & 0xFFFF
    if op == 0x1E:
        return "lq     %s, %d(%s)" % (R[rt], sx(imm), R[rs])
    if op == 0x1F:
        return "sq     %s, %d(%s)" % (R[rt], sx(imm), R[rs])
    if op == 0x12:
        sub = (w >> 21) & 31
        if sub == 2:
            return "cfc2   %s, vi%d" % (R[rt], (w >> 11) & 31)
        if sub == 6:
            return "ctc2   %s, vi%d" % (R[rt], (w >> 11) & 31)
        if sub == 1:
            return "qmfc2  %s, vf%d" % (R[rt], (w >> 11) & 31)
        if sub == 5:
            return "qmtc2  %s, vf%d" % (R[rt], (w >> 11) & 31)
        if sub >= 16:
            return "vu0macro %08x" % w
    if op == 0x1C:
        return "mmi    %08x" % w
    ins = list(md.disasm(struct.pack("<I", w), a))
    if ins:
        return "%-6s %s" % (ins[0].mnemonic, ins[0].op_str)
    return ".word  %08x" % w


if __name__ == "__main__":
    for i in range(cnt):
        a = start + 4 * i
        w = struct.unpack_from("<I", d, TOFF + (a - TB))[0]
        print("%06x: %08x  %s" % (a, w, dis(a)))
