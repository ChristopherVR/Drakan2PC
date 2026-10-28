#!/usr/bin/env python
"""Static summary of the Drakan VU1 microprogram (analysis/vu/vu1_micromem.bin).
python -I vu_analyze.py <vu1_micromem.bin>"""

import collections
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import vudis

buf = open(sys.argv[1], "rb").read()
ins, labels, text = vudis.disassemble(buf, 0)
n = len(ins)
print("instructions: %d (bytes %#x)" % (n, len(buf)))
hist = collections.Counter()
for x in ins:
    m = x["upper"].split()[0].split(".")[0]
    hist["U:" + m] += 1
    if x["up"] & 0x80000000:
        hist["L:LOI"] += 1
    else:
        hist["L:" + x["lower"].split()[0].split(".")[0]] += 1
print("\nopcode histogram (excluding NOP):")
for k, v in sorted(hist.items(), key=lambda t: (-t[1], t[0])):
    if k not in ("U:NOP", "L:NOP"):
        print("  %-12s %d" % (k, v))
print("\nE-bit instructions:", " ".join("%04x" % x["pc"] for x in ins if "E" in x["flags"]))
print("I-bit (LOI) constants:")
for x in ins:
    if "I" in x["flags"]:
        print("  %04x %s" % (x["pc"], x["lower"]))
print("M/D/T bits:", [(("%04x" % x["pc"]), "".join(x["flags"])) for x in ins if set(x["flags"]) & set("MDT")])
print("XGKICK:", " ".join("%04x" % x["pc"] for x in ins if "XGKICK" in x["lower"]))
print("JR/JALR/BAL:", [(("%04x" % x["pc"]), x["lower"]) for x in ins if x["kind"] in ("jr", "jalr", "bal")])
print(
    "flag-reading ops (CLIP/FCGET/FMAND/FSAND/FCAND..):",
    collections.Counter(
        x["lower"].split()[0]
        for x in ins
        if x["lower"].split()[0]
        in ("FCGET", "FMAND", "FSAND", "FCAND", "FMEQ", "FSEQ", "FCEQ", "FCOR", "FMOR", "FSOR", "FCSET", "FSSET")
    ),
)
print("CLIP count:", sum(1 for x in ins if x["upper"].startswith("CLIP")))
print(
    "DIV/SQRT/RSQRT:",
    collections.Counter(x["lower"].split()[0] for x in ins if x["lower"].split()[0] in ("DIV", "SQRT", "RSQRT")),
)
print(
    "E-unit (P reg) ops:",
    collections.Counter(x["lower"].split()[0] for x in ins if re.match(r"E[A-Z]+|MFP|WAITP", x["lower"].split()[0])),
)
# vi00-based absolute loads/stores -> VU data memory map
abs_ld = collections.defaultdict(set)
for x in ins:
    m = re.match(r"(LQ|SQ|ILW|ISW)\.(\w+) (?:vf|vi)\d+, (-?\d+)\(vi00\)", x["lower"])
    if m:
        abs_ld[int(m.group(3))].add(m.group(1) + "." + m.group(2))
print("\nabsolute VU data-memory addresses accessed through vi00 (addr: ops):")
for a in sorted(abs_ld):
    print("  %4d (0x%03x): %s" % (a, a, ",".join(sorted(abs_ld[a]))))
print("\nbranch targets / labels:", len(labels))
