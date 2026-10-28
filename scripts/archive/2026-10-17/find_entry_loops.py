#!/usr/bin/env python3
"""List the recompiled functions whose loop back-edge targets their own first instruction (a `label_<entry>:` inside the function).

These are the functions that a checkpoint (preemption) unwinds with ctx->pc == entry pc, the pattern PS2Runtime::dispatchGuestBranch used to
mistake for "callee returned" (docs/journal/2026-10-17/crash-listener.md). Usage:  python scripts/archive/2026-10-17/find_entry_loops.py build/recomp_out/gen
"""

import glob
import re
import sys

gen = sys.argv[1] if len(sys.argv) > 1 else "build/recomp_out/gen"
found = []
for path in sorted(glob.glob(gen + "/ps2_recomp_*.cpp")):
    text = open(path, encoding="utf-8", errors="replace").read()
    for part in re.split(r"\n// Function: ", text)[1:]:
        m = re.match(r"(\S+)\n// Address: 0x([0-9a-f]+) - 0x([0-9a-f]+)", part)
        if not m:
            continue
        name, lo, hi = m.groups()
        if re.search(r"^label_%s:" % lo, part, re.M):
            found.append((int(lo, 16), int(hi, 16), name))
for lo, hi, name in found:
    print("0x%06x-0x%06x %4d bytes  %s" % (lo, hi, hi - lo, name))
print("%d functions loop to their own entry" % len(found))
