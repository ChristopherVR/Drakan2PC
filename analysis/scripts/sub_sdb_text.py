"""Dump subtitle text carried in SDB sound descriptors (SRSC type 0x302).

Record (verified on every English/French/German SDB of the disc):
  u16 namelen, name[namelen] (NUL padded, e.g. "C404NM05-A"), then, with base = 2 + namelen:
  base+0   u32 flags          bit 0x2000 = descriptor carries a subtitle string (0x2002 / 0x2012 seen; text may still be empty)
  base+4   u32 0x00100001
  base+8   u32 sample rate    (0x5622 = 22050)
  base+12  s32 pitch/detune   (-309 ...)
  base+16  u32 0
  base+20  f32 (15.0 = 0x41700000 for dialogue, 20.0 for SFX)
  base+24  u32 0
  base+28  u32 0x55
  base+32  u16 text length (bytes, including NUL padding)   -- only if flags & 0x2000 (otherwise u32 0x2 ...)
  base+34  text, cp1252, NUL padded
The SDB resource ids are 0x302 (descriptor, this record), 0x304 (SPU ADPCM data of the same id/sub), 0x305 (ADPCM stream header).

usage: sub_sdb_text.py FILE.SDB [--all]   -> prints id|sub|name|text
As module: iter_lines(path) -> yields dict(id, sub, name, flags, text, rec, off)
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc

NUL = bytes([0])


def parse_302(rec):
    n = struct.unpack_from("<H", rec, 0)[0]
    name = rec[2 : 2 + n].split(NUL)[0].decode("latin1")
    return n, name


def iter_lines(path, want_all=False):
    d = open(path, "rb").read()
    m, v, idx, cnt, ents = iop_srsc.parse(d)
    for t, i, s, o, sz in ents:
        if t != 0x302:
            continue
        rec = d[o : o + sz]
        n, name = parse_302(rec)
        base = 2 + n
        flags = struct.unpack_from("<I", rec, base)[0]
        text = None
        if flags & 0x2000 and len(rec) >= base + 34:
            L = struct.unpack_from("<H", rec, base + 32)[0]
            text = rec[base + 34 : base + 34 + L].split(NUL)[0].decode("cp1252")
        yield dict(id=i, sub=s, name=name, flags=flags, text=text, rec=rec, off=o)


if __name__ == "__main__":
    for e in iter_lines(sys.argv[1]):
        if e["text"] or "--all" in sys.argv:
            print("%d|%d|%s|%s" % (e["id"], e["sub"], e["name"], e["text"]))
