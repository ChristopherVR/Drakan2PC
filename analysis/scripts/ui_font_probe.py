"""Dump palette-index map of INTERFCE.TXD font 97 (stored rows) around the arrow line: ui_font_probe.py INTERFCE.TXD [id]"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc
import txd_decode as T

d = open(sys.argv[1], "rb").read()
tid = int(sys.argv[2]) if len(sys.argv) > 2 else 97
m, v, idx, cnt, ents = iop_srsc.parse(d)
for t, i, s, o, sz in ents:
    if t == 0x40 and i == tid:
        b = d[o : o + sz]
        w, h = 256, 256
        pix = T.HDR + 16 + 1024
        for y in range(0, 62):  # stored rows
            row = b[pix + y * w : pix + (y + 1) * w]
            print(
                "%2d " % y
                + "".join("." if c == row[0] and False else ("%x" % (c % 16) if c else ".") for c in row[:100])
            )
        break
