"""Drakan .XDB text database dumper (SRSC type 0x400 string sets, 0x330 names).

Type 0x330: u16 len + name bytes ("Old Text", "Controller Text", ...) = name of the group with the same id.
Type 0x400: 8 slots, each u16 len + len bytes. len = roundup4(chars + 2) - 2 (so u16+bytes stays 4-aligned);
  the string is NUL-terminated only when there is padding. Slot order = language index
  0 English, 1 German, 2 French, 3 Italian, 4 Spanish, 5..7 empty (len 2). (Some entries, e.g. level paths, are not text.)
Type 0x500/0x402: small headers (languages / count).

usage: xdb_dump.py FILE.XDB [lang=0]   (lang index, or 'all')
As a module: parse_xdb(bytes) -> list of dict(id, sub, name, strings[8]); encode_strings(list[bytes]) -> bytes
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc


def decode_strings(b):
    out, p = [], 0
    while p + 2 <= len(b) and len(out) < 8:
        n = struct.unpack_from("<H", b, p)[0]
        s = b[p + 2 : p + 2 + n]
        out.append(s.split(b"\0")[0])
        p += 2 + n
    return out, p


def encode_strings(strs):
    """strs: list of bytes (up to 8, padded with b''). Inverse of decode_strings."""
    out = b""
    for s in list(strs) + [b""] * (8 - len(strs)):
        n = ((len(s) + 2 + 3) & ~3) - 2
        out += struct.pack("<H", n) + s + b"\0" * (n - len(s))
    return out


def parse_xdb(d):
    m, v, idx, cnt, ents = iop_srsc.parse(d)
    names = {}
    res = []
    for t, i, s, o, sz in ents:
        if t == 0x330:
            n = struct.unpack_from("<H", d, o)[0]
            names[i] = d[o + 2 : o + 2 + n].split(b"\0")[0].decode("latin1")
    for t, i, s, o, sz in ents:
        if t == 0x400:
            strs, used = decode_strings(d[o : o + sz])
            res.append(dict(id=i, sub=s, name=names.get(i, ""), strings=strs, raw_size=sz, used=used))
    return res


if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    lang = sys.argv[2] if len(sys.argv) > 2 else "0"
    for e in parse_xdb(d):
        st = e["strings"]
        if lang == "all":
            txt = " | ".join(x.decode("latin1") for x in st[:5])
        else:
            txt = st[int(lang)].decode("latin1")
        print("%3d sub %-2d [%s] %r" % (e["id"], e["sub"], e["name"], txt))
