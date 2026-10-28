"""List the ROMDIR entries of an IOPRP image (IOP reset image) and optionally extract to a dir (arg2)."""

import os
import struct
import sys

d = open(sys.argv[1], "rb").read()
i = d.find(b"RESET\0\0\0\0\0")
print("size", len(d), "RESET entry at", i)
off = 0
ents = []
p = i
while True:
    nm = d[p : p + 10].rstrip(b"\0").decode("latin1")
    ex, sz = struct.unpack_from("<HI", d, p + 10)
    if nm == "":
        break
    ents.append((nm, ex, sz))
    p += 16
pos = 0
out = sys.argv[2] if len(sys.argv) > 2 else None
if out:
    os.makedirs(out, exist_ok=True)
for nm, ex, sz in ents:
    print("%-12s extinfo=%-3d size=%-8d off=%#x" % (nm, ex, sz, pos))
    if out and nm not in ("-", "ROMDIR", "EXTINFO"):
        open(os.path.join(out, nm), "wb").write(d[pos : pos + sz])
    pos += (sz + 15) & ~15
