"""Split an address range of ee.asm into functions (addiu sp,sp,-N prologue / after jr ra) and summarise: size, callees, a1 constant before CallRpc(0x4bf068), string-ish lui/addiu refs.
usage: iop_eefuncs.py ee.asm lo hi"""

import re
import sys

lo = int(sys.argv[2], 16)
hi = int(sys.argv[3], 16)
L = []
for l in open(sys.argv[1]):
    m = re.match(r"([0-9a-f]+):\s+(\S+)\s*(.*)", l)
    if m:
        a = int(m.group(1), 16)
        if lo <= a < hi:
            L.append((a, m.group(2), m.group(3).strip()))
starts = []
prev_jr = True
for i, (a, mn, op) in enumerate(L):
    if (mn == "addiu" and op.startswith("$sp, $sp, -")) or (prev_jr and mn not in ("nop",)):
        if not starts or a - starts[-1] > 8:
            starts.append(a)
    prev_jr = (mn == "jr" and op == "$ra") or (prev_jr and mn == "nop")
    if mn == "jr" and op == "$ra":
        prev_jr = True
    elif mn not in ("nop",) and not (mn == "jr"):
        # delay slot after jr: keep prev_jr True one more instr
        pass
starts = sorted(set(starts))
idx = {a: i for i, (a, _, _) in enumerate(L)}
for si, s in enumerate(starts):
    e = starts[si + 1] if si + 1 < len(starts) else hi
    body = [x for x in L if s <= x[0] < e]
    calls = [x[2] for x in body if x[1] == "jal"]
    fno = []
    for k, x in enumerate(body):
        if x[1] == "jal" and x[2] == "0x4bf068":
            for y in reversed(body[max(0, k - 12) : k]):
                if re.match(r"\$a1,", y[2]) and y[1] in ("addiu", "ori", "move", "li"):
                    fno.append(y[1] + " " + y[2])
                    break
    print("%x size=%d calls=%s fno=%s" % (s, (e - s) // 4, ",".join(sorted(set(calls)))[:90], fno))
