"""Disassemble EE .text with capstone (MIPS64 approx; MMI/VU0 show as .word). usage: iop_eedis.py ELF out.asm"""

import sys

from capstone import *

d = open(sys.argv[1], "rb").read()
code = d[0x1000 : 0x1000 + 0x3D73F4]
md = Cs(CS_ARCH_MIPS, CS_MODE_MIPS64 + CS_MODE_LITTLE_ENDIAN)
md.skipdata = True
with open(sys.argv[2], "w") as f:
    for i in md.disasm(code, 0x100000):
        f.write("%06x:  %-8s %s\n" % (i.address, i.mnemonic if i.mnemonic != ".byte" else ".word", i.op_str))
