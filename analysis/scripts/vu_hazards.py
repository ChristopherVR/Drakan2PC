#!/usr/bin/env python
"""Find integer-register write -> branch-read hazards (writer 1 or 2 instrs before the branch)
in a VU microprogram binary.  python -I vu_hazards.py vu1_micromem.bin"""

import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vudis

buf = open(sys.argv[1], "rb").read()
ins, labels, _ = vudis.disassemble(buf, 0)


def writes(x):
    if x["up"] & 0x80000000:
        return set()
    t = x["lower"]
    m = t.split()[0].split(".")[0]
    r = set()
    ops = re.findall(r"vi(\d+)", t)
    if m in (
        "IADDIU",
        "ISUBIU",
        "IADDI",
        "ILW",
        "ILWR",
        "MTIR",
        "FMAND",
        "FSAND",
        "FSEQ",
        "FMEQ",
        "FMOR",
        "FSOR",
        "FCGET",
        "XTOP",
        "XITOP",
        "FCEQ",
    ):
        r.add(int(ops[0]) if ops else 1)
    elif m in ("IADD", "ISUB", "IAND", "IOR"):
        r.add(int(ops[0]))
    elif m in ("LQI", "SQI", "LQD", "SQD"):
        r.add(int(ops[0]))
    elif m in ("FCAND", "FCOR"):
        r.add(1)
    return {v for v in r if v}


def reads(x):
    t = x["lower"]
    if x["kind"] == "bc":
        return {int(v) for v in re.findall(r"vi(\d+)", t)}
    return set()


n = 0
for i, x in enumerate(ins):
    rd = reads(x)
    if not rd:
        continue
    for k in (1, 2):
        if i - k < 0:
            break
        w = writes(ins[i - k]) & rd
        if w:
            n += 1
            print(
                "%04x %-28s reads vi%s written %d instr(s) before at %04x: %s"
                % (x["pc"], x["lower"], sorted(w), k, ins[i - k]["pc"], ins[i - k]["lower"])
            )
print("hazard sites:", n)
