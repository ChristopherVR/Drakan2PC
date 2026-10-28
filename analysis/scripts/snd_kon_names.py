"""Names every key-on (SPU RAM address -> resource request that uploaded it -> SDB 0x302 descriptor name).
usage: python -I snd_kon_names.py <run.log> <extracted ISO folder> [min_time_s]   (DRAKAN_SND_LOG=3 DRAKAN_HLE_LOG=100000)"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc

log = sys.argv[1]
root = sys.argv[2]
tmin = float(sys.argv[3]) if len(sys.argv) > 3 else 0
names = {}


def load(sdb):
    if sdb in names:
        return names[sdb]
    f = os.path.join(root, sdb.replace("\\", "/"))
    n = {}
    if os.path.exists(f):
        d = open(f, "rb").read()
        for t, i, s, o, sz in iop_srsc.parse(d)[4]:
            if t == 0x302:
                l = struct.unpack_from("<H", d, o)[0]
                n[i] = d[o + 2 : o + 2 + l].decode("latin1").strip("\0")
    names[sdb] = n
    return n


lines = open(log, errors="replace").read().replace("\r", "\n").split("\n")
lst = []
for k, line in enumerate(lines):
    m = re.match(r"\[hle\] resource: (\S+?):? 0304/(\d+)/-1 \+0 len (\d+) -> type1 dest ([0-9a-f]+)", line)
    if m:
        lst.append((k, int(m.group(4), 16), (m.group(1), int(m.group(2)), int(m.group(3)))))
for k, line in enumerate(lines):
    m = re.match(r"\[spu ([0-9.]+)\]   KON (c\d\.v\d+) ssa=([0-9a-f]+) .*vol=(\w+)/(\w+) adsr=(\S+)", line)
    if not m:
        continue
    a = int(m.group(3), 16)
    if a == 0x5000 or float(m.group(1)) < tmin:
        continue
    cand = [x for x in lst if x[1] == a and abs(x[0] - k) < 8]
    if not cand:
        cand = [x for x in lst if x[1] == a and x[0] < k][-1:]
    nm = ""
    if cand:
        r = cand[0][2]
        nm = "%s %d %s len=%d" % (os.path.basename(r[0]), r[1], load(r[0]).get(r[1], "?"), r[2])
    print(
        "%7.2f KON %s ssa=%s vol=%s adsr=%s %s"
        % (float(m.group(1)), m.group(2), m.group(3), m.group(4), m.group(6), nm)
    )
