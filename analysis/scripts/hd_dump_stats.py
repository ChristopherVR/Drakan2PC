r"""Statistics / sanity checks of a gsr texture dump folder (<texdir>/dump/<hash>.png).
usage: hd_dump_stats.py <dump dir> [other dump dir]    second dir: reports hashes only in one of them (run-to-run stability)
Reports: count, size histogram, fully opaque vs alpha, near-duplicates (same RGB bytes, different alpha -> TEXA/CLUT variants),
non-power-of-two sizes, tiny/odd textures (fonts, partial uploads)."""

import collections
import glob
import hashlib
import os
import sys

import numpy as np
from PIL import Image


def load(d):
    r = {}
    for p in glob.glob(os.path.join(d, "*.png")):
        r[os.path.basename(p)[:16]] = p
    return r


a = load(sys.argv[1])
print("textures", len(a))
hist = collections.Counter()
alpha = 0
rgbgrp = collections.defaultdict(list)
npot = []
tot = 0
for h, p in a.items():
    im = Image.open(p).convert("RGBA")
    w, hh = im.size
    hist["%dx%d" % (w, hh)] += 1
    tot += w * hh
    arr = np.asarray(im)
    if (arr[..., 3] < 255).any():
        alpha += 1
    rgbgrp[hashlib.md5(arr[..., :3].tobytes() + bytes([w & 255, hh & 255])).hexdigest()].append(h)
    if (w & (w - 1)) or (hh & (hh - 1)):
        npot.append((h, w, hh))
print("with alpha", alpha, "pixels", tot)
for k, v in hist.most_common(20):
    print("  %-9s %d" % (k, v))
dups = [v for v in rgbgrp.values() if len(v) > 1]
print("same-RGB different-hash groups:", len(dups), "files", sum(len(v) for v in dups))
print("non power of two:", npot[:10])
if len(sys.argv) > 2:
    b = load(sys.argv[2])
    sa, sb = set(a), set(b)
    print("other dump", len(b), "common", len(sa & sb), "only first", len(sa - sb), "only second", len(sb - sa))
