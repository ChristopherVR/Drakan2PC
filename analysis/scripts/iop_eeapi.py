"""List jal targets in [lo,hi) called from outside [lo,hi): usage iop_eeapi.py ee.asm lo hi"""

import collections
import re
import sys

lo = int(sys.argv[2], 16)
hi = int(sys.argv[3], 16)
c = collections.defaultdict(list)
for l in open(sys.argv[1]):
    m = re.match(r"([0-9a-f]+):\s+jal\s+0x([0-9a-f]+)", l)
    if m:
        a = int(m.group(1), 16)
        t = int(m.group(2), 16)
        if lo <= t < hi and not (lo <= a < hi):
            c[t].append(a)
for t in sorted(c):
    print("%x  %d callers  e.g. %s" % (t, len(c[t]), " ".join("%x" % x for x in c[t][:4])))
