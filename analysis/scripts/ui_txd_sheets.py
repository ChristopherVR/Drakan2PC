"""Contact sheets of every texture of every TXD under a language tree, for hunting controller button art.
usage: ui_txd_sheets.py ROOT(e.g. E:/ENGLISH) OUTDIR [tile=64] [cols=16] [rows=12]
Writes OUTDIR/<name>_<n>.png (sheet pages, tile label = texture id) and OUTDIR/index.txt (id, size, bpp per texture)."""

import glob
import os
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc
import txd_decode as T
from PIL import Image, ImageDraw

root, out = sys.argv[1], sys.argv[2]
tile = int(sys.argv[3]) if len(sys.argv) > 3 else 64
cols = int(sys.argv[4]) if len(sys.argv) > 4 else 16
rows = int(sys.argv[5]) if len(sys.argv) > 5 else 12
os.makedirs(out, exist_ok=True)
idx = open(os.path.join(out, "index.txt"), "w")
for path in sorted(glob.glob(os.path.join(root, "**", "*.TXD"), recursive=True)):
    name = os.path.basename(os.path.dirname(path)) + "_" + os.path.splitext(os.path.basename(path))[0]
    d = open(path, "rb").read()
    try:
        m, v, ix, cnt, ents = iop_srsc.parse(d)
    except Exception as e:
        print(name, "parse fail", e)
        continue
    texs = [e for e in ents if e[0] == 0x40]
    print(name, len(texs), "textures")
    page = 0
    i = 0
    while i < len(texs):
        sheet = Image.new("RGB", (cols * tile, rows * (tile + 10)), (60, 0, 80))
        dr = ImageDraw.Draw(sheet)
        for k in range(cols * rows):
            if i >= len(texs):
                break
            t, tid, sub, o, sz = texs[i]
            i += 1
            try:
                w, h, rgba, info = T.decode(d[o : o + sz])
            except Exception as e:
                idx.write("%s %d ERR %s\n" % (name, tid, e))
                continue
            idx.write("%s %d %dx%d bpp%d\n" % (name, tid, w, h, info["bpp"]))
            im = Image.frombytes("RGBA", (w, h), rgba)
            im.thumbnail((tile, tile), Image.NEAREST if max(w, h) <= tile else Image.LANCZOS)
            if max(w, h) < tile // 2 and max(w, h) >= 8:
                s = tile // max(w, h)
                im = im.resize((w * s, h * s), Image.NEAREST)
            x, y = (k % cols) * tile, (k // cols) * (tile + 10)
            bg = Image.new("RGBA", im.size, (60, 0, 80, 255))
            bg.alpha_composite(im)
            sheet.paste(bg.convert("RGB"), (x, y + 10))
            dr.text((x + 1, y), str(tid), fill=(255, 255, 0))
        sheet.save(os.path.join(out, "%s_%d.png" % (name, page)))
        page += 1
