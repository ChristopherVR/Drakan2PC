"""Export selected textures upright (flipped, alpha over magenta-ish bg), scaled: ui_txd_export.py FILE.TXD OUT.png scale id [id...]  (montage)"""

import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc
import txd_decode as T
from PIL import Image, ImageDraw

d = open(sys.argv[1], "rb").read()
out = sys.argv[2]
sc = int(sys.argv[3])
ids = [int(x) for x in sys.argv[4:]]
m, v, ix, cnt, ents = iop_srsc.parse(d)
ims = []
for t, tid, sub, o, sz in ents:
    if t == 0x40 and tid in ids:
        w, h, rgba, info = T.decode(d[o : o + sz])
        im = Image.frombytes("RGBA", (w, h), rgba).transpose(Image.FLIP_TOP_BOTTOM)
        bg = Image.new("RGBA", im.size, (60, 0, 80, 255))
        bg.alpha_composite(im)
        ims.append((tid, bg.convert("RGB").resize((w * sc, h * sc), Image.NEAREST)))
W = sum(i.width + 6 for _, i in ims)
H = max(i.height for _, i in ims) + 12
sheet = Image.new("RGB", (W, H), (30, 30, 30))
x = 0
dr = ImageDraw.Draw(sheet)
for tid, im in ims:
    sheet.paste(im, (x, 12))
    dr.text((x, 0), str(tid), fill=(255, 255, 0))
    x += im.width + 6
sheet.save(out)
