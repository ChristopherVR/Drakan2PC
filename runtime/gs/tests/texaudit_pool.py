"""Export every texture of the disc's ENGLISH/*.TXD files as a flat pool for texaudit.exe (ground truth).
usage: python -I texaudit_pool.py OUT_DIR      -> OUT_DIR/pool.bin, OUT_DIR/pool.txt
record: u32 file, u32 id, u32 w, u32 h, u32 bpp, u32 palkind, u32 palBytes, u32 pixBytes, palette raw bytes, pixel raw bytes
(palette raw = as stored in the TXD: kind 1 = 256 x B,G,R,A ; kind 2 = 256 x u16; pixel raw = linear rows as stored)"""

import os
import struct
import sys

here = os.path.dirname(os.path.abspath(__file__))
root = os.path.abspath(os.path.join(here, "..", "..", ".."))
sys.path.insert(0, os.path.join(root, "analysis", "scripts"))
sys.path.insert(0, os.path.join(root, "build", "captures"))
import iop_srsc
import isoread as I


def walk(lba, size, p, out):
    for n, l, s, d in I.lsdir(lba, size):
        if n in ("", "\x00", "\x01"):
            continue
        if d:
            walk(l, s, p + n + "/", out)
        elif n.upper().endswith(".TXD") and p.upper().startswith("ENGLISH/"):
            out.append((p + n, l, s))


def main():
    out = []
    walk(struct.unpack_from("<I", I.root, 2)[0], struct.unpack_from("<I", I.root, 10)[0], "", out)
    os.makedirs(sys.argv[1], exist_ok=True)
    pb = open(os.path.join(sys.argv[1], "pool.bin"), "wb")
    pt = open(os.path.join(sys.argv[1], "pool.txt"), "w")
    n = 0
    for fi, (name, lba, size) in enumerate(out):
        pt.write("%d %s\n" % (fi, name))
        d = I.sec(lba, (size + 2047) // 2048)[:size]
        m, v, idx, cnt, ents = iop_srsc.parse(d)
        for t, tid, sub, o, sz in ents:
            if t != 0x40:
                continue
            b = d[o : o + sz]
            w, h, stride, bpp, flags = struct.unpack_from("<5I", b, 0)
            pal = b""
            kind = 0
            if bpp == 8:
                kind, cnt2 = struct.unpack_from("<II", b, 0x40)
                if kind == 1:
                    pal = b[0x50 : 0x50 + 1024]
                elif kind == 2:
                    pal = b[0x50 : 0x50 + 512]
                else:
                    continue
                pix = b[0x50 + len(pal) :]
            else:
                pix = b[0x40:]
            if bpp not in (8, 16, 24, 32) or len(pix) != w * h * bpp // 8:
                continue
            pb.write(struct.pack("<8I", fi, tid, w, h, bpp, kind, len(pal), len(pix)) + pal + pix)
            n += 1
        print(name, n, flush=True)
    pb.close()
    pt.close()


main()
