"""Compares how long each voice played (key-on -> next key-off / re-key-on) with the length of its sample at its pitch.
usage: python -I snd_voice_life.py <run.log> <extracted ISO folder>   (DRAKAN_SND_LOG=3)"""

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
ev = []  # (t, kind, core, mask/voice, ssa, k)
for k, line in enumerate(lines):
    m = re.match(r"\[spu ([0-9.]+)\]   KON c(\d)\.v(\d+) ssa=([0-9a-f]+) pitch=(\w+)", line)
    if m:
        ev.append(
            (float(m.group(1)), "KON", int(m.group(2)), int(m.group(3)), int(m.group(4), 16), k, int(m.group(5), 16))
        )
        continue
    m = re.match(r"\[spu ([0-9.]+)\] KOFF core(\d) mask=([0-9a-f]+)", line)
    if m:
        ev.append((float(m.group(1)), "KOFF", int(m.group(2)), int(m.group(3), 16), 0, k, 0))
for i, e in enumerate(ev):
    if e[1] != "KON" or e[4] == 0x5000:
        continue
    cand = [x for x in lst if x[1] == e[4] and abs(x[0] - e[5]) < 8] or [
        x for x in lst if x[1] == e[4] and x[0] < e[5]
    ][-1:]
    nm, ln = "?", 0
    if cand:
        r = cand[0][2]
        nm = "%s %d %s" % (os.path.basename(r[0]), r[1], load(r[0]).get(r[1], "?"))
        ln = r[2]
    dur = ln / 16 * 28 / (e[6] / 4096 * 48000) if ln and e[6] else 0
    end = None
    for x in ev[i + 1 :]:
        if x[1] == "KOFF" and x[2] == e[2] and (x[3] >> e[3]) & 1:
            end = x[0]
            break
        if x[1] == "KON" and x[2] == e[2] and x[3] == e[3]:
            end = x[0]
            break
    print(
        "%8.2f c%d.v%-2d len=%6d expected=%6.2fs actual=%s %s"
        % (e[0], e[2], e[3], ln, dur, ("%.2fs" % (end - e[0])) if end else "-", nm)
    )
