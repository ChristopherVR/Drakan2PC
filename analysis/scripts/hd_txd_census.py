r"""Census of every texture in the English TXD files: counts, unique decoded images, size histogram, pixels (cost of x2/x4 upscaling).
usage: hd_txd_census.py <dir with ENGLISH TXD tree> [report.txt]
A texture the game uploads is decoded by gsr (CLUT + TEXA) before hashing, so 'unique decoded RGBA' here is a good estimate of the number of
distinct HD-pack files (a few more exist: runtime-generated / render-target textures are excluded, palette-animated textures multiply)."""

import collections
import glob
import hashlib
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc
import txd_decode as T


def main(root, rep=None):
    seen = {}
    tot = 0
    bad = 0
    hist = collections.Counter()
    per = collections.Counter()
    pix = 0
    bpp = collections.Counter()
    for p in sorted(glob.glob(os.path.join(root, "**", "*.TXD"), recursive=True)):
        d = open(p, "rb").read()
        _, _, _, _, ents = iop_srsc.parse(d)
        for t, i, s, o, z in ents:
            if t != 0x40:
                continue
            tot += 1
            try:
                w, h, rgba, info = T.decode(d[o : o + z])
            except Exception:
                bad += 1
                continue
            k = hashlib.md5(rgba + bytes([w & 255, w >> 8, h & 255, h >> 8])).hexdigest()
            per[os.path.basename(p)] += 1
            bpp[info.get("bpp")] += 1
            if k not in seen:
                seen[k] = (w, h)
                hist["%dx%d" % (w, h)] += 1
                pix += w * h
    print("textures", tot, "undecodable", bad, "unique decoded", len(seen))
    print("bpp", dict(bpp))
    print("unique pixels", pix, "  x4 pixels", pix * 16, " = %.1f GB RGBA at 4x" % (pix * 16 * 4 / 1e9))
    print("size histogram (unique):")
    for k, v in hist.most_common(25):
        print("  %-9s %d" % (k, v))
    print("per file:", dict(per.most_common(40)))


if __name__ == "__main__":
    main(*sys.argv[1:])
