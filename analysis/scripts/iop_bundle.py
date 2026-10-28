"""Split a decrypted LEVELS.RRC bundle (output of iop_rrc.py) into its IRX modules.
Layout: u32 count, u32 offset[count] (absolute, into the inflated buffer), then the ELFs (16-byte padded).
usage: iop_bundle.py LEVELS.RRC.dec outdir"""

import os
import struct
import sys

d = open(sys.argv[1], "rb").read()
out = sys.argv[2]
os.makedirs(out, exist_ok=True)
n = struct.unpack_from("<I", d, 0)[0]
offs = [struct.unpack_from("<I", d, 4 + 4 * i)[0] for i in range(n)] + [len(d)]
names = ["sio2man", "padman", "libsd", "sdrdrv", "mcman", "mcserv", "rsrciop"]  # load order as used by EE 0x101710
for i in range(n):
    p = os.path.join(out, "mod%d_%s.irx" % (i, names[i] if i < len(names) else "x"))
    open(p, "wb").write(d[offs[i] : offs[i + 1]])
    print(p, offs[i + 1] - offs[i])
