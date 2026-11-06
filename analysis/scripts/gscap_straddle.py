"""Count triangles per GIF path whose vertices straddle the camera plane (mixed sign of Q), from a DRKGSCAP file.
usage: python gscap_straddle.py file.bin   (see docs/journal/2026-11-06/stratos-garbled-effects.md)
"""

import collections
import struct
import sys

f = open(sys.argv[1], "rb").read()
p = 32
frame = -1
res = collections.Counter()
ex = []


def fl(w):
    return struct.unpack("<f", struct.pack("<I", w & 0xFFFFFFFF))[0]


while p + 8 <= len(f):
    t, n = struct.unpack_from("<II", f, p)
    p += 8
    pl = f[p : p + n]
    p += n
    if t == 1:
        frame = struct.unpack_from("<Q", pl)[0]
    elif t == 2:
        path, flags, _, size = struct.unpack_from("<BBHI", pl)
        d = pl[8:]
        q = 0
        while q + 16 <= len(d):
            lo, hi = struct.unpack_from("<QQ", d, q)
            q += 16
            nloop = lo & 0x7FFF
            flg = (lo >> 58) & 3
            nreg = (lo >> 60) & 15 or 16
            prim = (lo >> 47) & 0x7FF if (lo >> 46) & 1 else None
            if flg == 0:
                regs = [(hi >> (4 * i)) & 15 for i in range(nreg)]
                if 2 in regs and 5 in regs and prim is not None:
                    pt = prim & 7
                    si = regs.index(2)
                    xi = regs.index(5)
                    qs = []
                    zs = []
                    ad = []
                    xy = []
                    for v in range(nloop):
                        sw = struct.unpack_from("<4I", d, q + (v * nreg + si) * 16)
                        qs.append(fl(sw[2]))
                        x = struct.unpack_from("<4I", d, q + (v * nreg + xi) * 16)
                        zs.append(x[2])
                        ad.append((x[3] >> 15) & 1)
                        xy.append((x[0] & 0xFFFF, x[1] & 0xFFFF))
                    tris = []
                    for v in range(2, nloop):
                        if ad[v] or pt < 3:
                            continue
                        if pt == 3 and v % 3 != 2:
                            continue
                        a = 0 if pt == 5 else v - 2
                        tris.append((a, v - 1, v))
                    for tr in tris:
                        s = [qs[i] > 0 for i in tr]
                        res[(path, "mixed" if 0 < sum(s) < 3 else ("allneg" if not any(s) else "allpos"))] += 1
                        if 0 < sum(s) < 3 and len(ex) < 8:
                            ex.append(
                                (
                                    frame,
                                    path,
                                    size,
                                    nloop,
                                    hex(prim),
                                    [round(qs[i], 4) for i in tr],
                                    [hex(zs[i]) for i in tr],
                                    [xy[i] for i in tr],
                                )
                            )
                q += 16 * nloop * nreg
            elif flg == 1:
                q += 8 * ((nloop * nreg + 1) // 2) * 2
            elif flg == 2:
                q += 16 * nloop
print(dict(res))
for e in ex:
    print(e)
