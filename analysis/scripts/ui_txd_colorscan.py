"""Scan all TXDs for PS2 button-icon colours (sampled from INTERFCE font 97 icons). ui_txd_colorscan.py ROOT
Prints textures having >= MIN pixels of >= 2 of the 4 icon colours (red circle, pink square, green triangle, blue cross)."""

import glob
import os
import sys

import numpy as np

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc
import txd_decode as T

root = sys.argv[1]
MIN = 12


def load(path):
    d = open(path, "rb").read()
    m, v, ix, cnt, ents = iop_srsc.parse(d)
    return d, [e for e in ents if e[0] == 0x40]


d, ents = load(os.path.join(root, "COMMON/INTERFCE/INTERFCE.TXD"))
e = next(x for x in ents if x[1] == 97)
w, h, rgba, _ = T.decode(d[e[3] : e[3] + e[4]])
a = np.frombuffer(rgba, np.uint8).reshape(h, w, 4).astype(int)
# icon boxes stored coords (x, y_stored, 22x21): circle 136, square 161, triangle 187, cross 215 at y 36
cols = {}
for name, x0 in (("circle", 136), ("square", 161), ("triangle", 187), ("cross", 215)):
    box = a[36:57, x0 : x0 + 22]
    flat = box.reshape(-1, 4)
    sat = flat[:, :3].max(1) - flat[:, :3].min(1)
    sel = flat[(sat > 70) & (flat[:, 3] > 200)]
    c = np.median(sel[:, :3], axis=0)
    cols[name] = c
    print(name, c, len(sel))
hits = 0
for path in sorted(glob.glob(os.path.join(root, "**", "*.TXD"), recursive=True)):
    try:
        d, ents = load(path)
    except Exception:
        continue
    for t, tid, sub, o, sz in ents:
        try:
            w, h, rgba, info = T.decode(d[o : o + sz])
        except Exception:
            continue
        if "INTERFCE" in path and tid == 97:
            continue
        px = np.frombuffer(rgba, np.uint8).reshape(-1, 4).astype(int)
        opaque = px[:, 3] > 200
        n = 0
        desc = []
        for name, c in cols.items():
            dist = np.abs(px[:, :3] - c).sum(1)
            k = int(((dist < 24) & opaque).sum())
            if k >= MIN:
                n += 1
                desc.append("%s:%d" % (name, k))
        if n >= 2:
            hits += 1
            print(os.path.relpath(path, root), tid, "%dx%d" % (w, h), " ".join(desc))
print("hits", hits)
