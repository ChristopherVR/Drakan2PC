"""Sheet of all small (<=32 px) textures of all TXDs under ROOT, upright, 2x: ui_txd_small.py ROOT OUTPREFIX"""

import glob
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc
import txd_decode as T
from PIL import Image, ImageDraw

root, outp = sys.argv[1], sys.argv[2]
items = []
for path in sorted(glob.glob(os.path.join(root, "**", "*.TXD"), recursive=True)):
    name = os.path.splitext(os.path.basename(path))[0]
    d = open(path, "rb").read()
    try:
        m, v, ix, cnt, ents = iop_srsc.parse(d)
    except Exception:
        continue
    for t, tid, sub, o, sz in ents:
        if t != 0x40:
            continue
        w, h = int.from_bytes(d[o : o + 4], "little"), int.from_bytes(d[o + 4 : o + 8], "little")
        if w > 32 or h > 64 or ((w, h) == (32, 64) and False):
            continue
        if w * h > 32 * 64:
            continue
        try:
            W, H, rgba, info = T.decode(d[o : o + sz])
        except Exception:
            continue
        items.append((name, tid, Image.frombytes("RGBA", (W, H), rgba).transpose(Image.FLIP_TOP_BOTTOM)))
print(len(items))
cols, rows, cw, ch = 16, 9, 70, 84
for p in range(0, len(items), cols * rows):
    sheet = Image.new("RGB", (cols * cw, rows * ch), (60, 0, 80))
    dr = ImageDraw.Draw(sheet)
    for k, (name, tid, im) in enumerate(items[p : p + cols * rows]):
        x, y = (k % cols) * cw, (k // cols) * ch
        bg = Image.new("RGBA", im.size, (60, 0, 80, 255))
        bg.alpha_composite(im)
        sheet.paste(bg.convert("RGB").resize((im.width * 2, im.height * 2), Image.NEAREST), (x + 1, y + 18))
        dr.text((x + 1, y), str(tid), fill=(255, 255, 0))
        dr.text((x + 1, y + 8), name[:9], fill=(160, 255, 160))
    sheet.save("%s_%d.png" % (outp, p // (cols * rows)))
