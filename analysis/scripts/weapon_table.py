"""Harvest the weapon / ammunition / spell item records from guest RAM dumps (*.ram, 32 MiB, PAL).

  python -I analysis/scripts/weapon_table.py [--max N] DIR_OR_RAM...

Every live actor-registry handle of type 10 (weapons) and 11 (usables) is read as docs/reference/world-model.md
describes: record `rec = *(res+8) + ix*0x24` (+0 resource id, +4 type, +8 class), property block `*(h+0x54+4)`,
`prop(id) = *(data+id*4)`. Prints one row per resource id: class, type, stance (prop 0x1e), damage (prop 0x1a),
name string id (prop 0) and its text when resident in the dump, base price (prop 0x11), skill (0x13) and the number of dumps it was seen in. The result
feeds `drakanWeaponDamageLimits()` in runtime/net/include/net/MatchServer.h (see docs/journal/2026-10-31/pvp.md).
"""

import argparse
import collections
import pathlib
import struct

NAMES = {}


def lookup(b, u, mgr, sid):
    if sid == 0 or not (0x100000 <= mgr < 0x2000000 - 0x400):
        return None
    e = u(mgr + (((sid * 0x59) & 0x7F) + 8) * 4)
    for _ in range(64):
        if not (0x100000 <= e < 0x2000000 - 0x20):
            return None
        if u(e) == sid:
            if (u(e + 8) & 3) != 3:
                return None
            t = u(e + 0x1C) & 0x1FFFFFF
            if t < 0x100000 or t >= 0x2000000 - 64:
                return None
            end = b.find(b"\x00", t, t + 64)
            return b[t:end].decode("latin-1") if end > t else None
        e = u(e + 0x14)
    return None


def scan(b, rows):
    def valid(a, n=4):
        return 0x100000 <= (a & 0x1FFFFFFF) <= len(b) - n

    def u(a):
        return struct.unpack_from("<I", b, a & 0x1FFFFFF)[0]

    def hw(a):
        return struct.unpack_from("<H", b, a & 0x1FFFFFF)[0]

    ct = u(0x5565D4)
    if not valid(ct, 0x4B8):
        return
    tb = u(ct + 0x7C)
    n = min(u(ct + 0x84), 10000)
    res = u(ct + 0x320)
    if not valid(res, 0x14) or not valid(tb, max(8, n * 8)):
        return
    rtab, rcount = u(res + 8), u(res + 0x10)
    for i in range(n):
        h = u(tb + i * 8)
        if not valid(h, 0x100) or u(h) != 0x51B8B0:
            continue
        typ = u(h + 0xC)
        if typ not in (10, 11):
            continue
        ix = hw(h + 0x10)
        if ix == 0xFFFF or ix > rcount:
            continue
        rec = rtab + ix * 0x24
        if not valid(rec, 0x24):
            continue
        blk = u(rec + 0xC)
        if not valid(blk, 12):
            continue
        pb = u(blk + 4)
        if not valid(pb, 0x400):
            continue

        def prop(pid, pb=pb):
            return u(pb + pid * 4)

        key = u(rec)
        nid = prop(0)
        if nid not in NAMES:
            nm = lookup(b, u, u(ct + 0x344), nid)
            if nm:
                NAMES[nid] = nm
        rows[key].append((u(rec + 8), typ, prop(0x1E), prop(0x1A), prop(0), prop(0x11), prop(0x13)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("paths", nargs="+")
    ap.add_argument("--max", type=int, default=300)
    a = ap.parse_args()
    files = []
    for p in map(pathlib.Path, a.paths):
        files += sorted(p.rglob("*.ram")) if p.is_dir() else [p]
    step = max(1, len(files) // a.max)
    rows = collections.defaultdict(list)
    seen = 0
    for f in files[::step]:
        b = f.read_bytes()
        if len(b) == 0x2000000:
            scan(b, rows)
            seen += 1
    print(f"dumps scanned: {seen}")
    print("resource  class    type stance damage nameid  price skill  dumps")
    for key, v in sorted(rows.items(), key=lambda kv: (kv[1][0][1], kv[1][0][0], kv[0])):
        c = collections.Counter(v).most_common(1)[0][0]
        print(
            f"{key:#08x}  {c[0]:#07x}  {c[1]:>4} {c[2]:>6} {c[3]:>6} {c[4]:#07x} {c[5]:>6} {c[6]:>5}  {len(v)}  "
            f"{NAMES.get(c[4], '')}"
        )


if __name__ == "__main__":
    main()
