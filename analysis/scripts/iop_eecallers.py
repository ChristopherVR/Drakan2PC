"""usage: iop_eecallers.py ee.asm 0xTARGET [before=8] -> print call sites with preceding context"""

import re
import sys

lines = open(sys.argv[1]).read().split("\n")
tgt = sys.argv[2]
before = int(sys.argv[3]) if len(sys.argv) > 3 else 8
for i, l in enumerate(lines):
    if re.search(r"\bjal\s+" + re.escape(tgt) + r"$", l):
        print("\n".join(lines[max(0, i - before) : i + 2]))
        print("--")
