"""Parse the SRSC resource container used by Drakan (.TXD/.MOD/.ODB/.ADB/.SDB/.XDB/.VDB/.LVL/.SSD/.RRC).
Header (16 bytes): 'SRSC', u16 ver(0x0101), u32 index_offset, u32 count, u16 pad.
Index @index_offset (runs to EOF): count * 14-byte entries {u16 type, u16 id, u16 sub, u32 offset, u32 size}
(deduced from RSRCIOP.IRX 0x20b8/0x26e4/0x21f4 and verified: every offset+size <= index_offset).
usage: iop_srsc.py file [n]"""

import collections
import struct
import sys


def parse(d):
    magic, ver, idx, cnt = struct.unpack_from("<4sHII", d, 0)
    ents = [struct.unpack_from("<HHHII", d, idx + 14 * i) for i in range(cnt)]
    return magic, ver, idx, cnt, ents


if __name__ == "__main__":
    d = open(sys.argv[1], "rb").read()
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 8
    magic, ver, idx, cnt, ents = parse(d)
    ok = all(o + s <= idx for t, i, s2, o, s in ents)
    print(
        magic,
        hex(ver),
        "index_off",
        hex(idx),
        "count",
        cnt,
        "size",
        len(d),
        "index bytes ok" if len(d) - idx == cnt * 14 else "INDEX SIZE MISMATCH",
        "ranges ok" if ok else "RANGE ERR",
    )
    print("types:", {hex(k): v for k, v in collections.Counter(e[0] for e in ents).items()})
    for e in ents[:n]:
        print(" type=%#06x id=%d sub=%d off=%#x size=%d" % e)
