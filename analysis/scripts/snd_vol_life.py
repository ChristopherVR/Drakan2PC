"""Per voice: sample name, number of volume writes and the highest VOLL written between its key-on and the next key-on of that voice.
A positional SFX that is "started" but has maxvol=0 for its whole life is inaudible (the sqrt.s translator bug looked exactly like this).
usage: python -I snd_vol_life.py <run.log> <extracted ISO folder holding ENGLISH/COMMON/.../*.SDB>
run.log = stderr of drakan.exe with DRAKAN_SND_LOG=3 DRAKAN_HLE_LOG=100000 (see docs/reference/sound-debug.md)"""

import os
import re
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc

log = sys.argv[1]
root = sys.argv[2]
names = {}


def load(sdb):
    if sdb in names:
        return names[sdb]
    f = os.path.join(root, sdb.replace(chr(92), "/"))
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
for k, l in enumerate(lines):
    m = re.match(r"\[hle\] resource: (\S+?):? 0304/(\d+)/-1 \+0 len (\d+) -> type1 dest ([0-9a-f]+)", l)
    if m:
        lst.append((k, int(m.group(4), 16), (m.group(1), int(m.group(2)))))
cur = {}
done = []
for k, l in enumerate(lines):
    m = re.match(r"\[spu ([0-9.]+)\]   KON c(\d)\.v(\d+) ssa=([0-9a-f]+)", l)
    if m:
        key = (int(m.group(2)), int(m.group(3)))
        if key in cur:
            done.append(cur.pop(key))
        a = int(m.group(4), 16)
        if a == 0x5000:
            continue
        c = [x for x in lst if x[1] == a and abs(x[0] - k) < 8] or [x for x in lst if x[1] == a and x[0] < k][-1:]
        nm = "?"
        if c:
            r = c[0][2]
            nm = "%s %d %s" % (os.path.basename(r[0]), r[1], load(r[0]).get(r[1], "?"))
        cur[key] = {"t": float(m.group(1)), "name": nm, "max": 0, "n": 0, "key": key}
        continue
    m = re.match(r"\[spu ([0-9.]+)\] setParam reg=0([01]) core(\d) voice(\d+) = 0x([0-9a-f]+)", l)
    if m:
        key = (int(m.group(3)), int(m.group(4)))
        if key in cur:
            v = int(m.group(5), 16)
            cur[key]["max"] = max(cur[key]["max"], v)
            cur[key]["n"] += 1
done += list(cur.values())
for d in done:
    print(
        "%7.2f c%d.v%-2d maxvol=%5x updates=%4d  %s" % (d["t"], d["key"][0], d["key"][1], d["max"], d["n"], d["name"])
    )
