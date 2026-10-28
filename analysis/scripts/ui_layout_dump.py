"""Dump the GUI screen layouts (type 0x600 entries of ENGLISH/DRAKAN2.RRC) with the property names / XDB strings they use.

usage: python -I ui_layout_dump.py <game.iso> <SCES_500.06>

Reads the disc image (ISO9660, no mount needed) and the ELF and prints, for every layout, its widgets: widget id, property index and
name ("Options - Controller" ...), the English XDB text that property resolves to (INTERFCE.ODB "User Interface Properties", verified
for the text properties 98..142), widget type and geometry. Format and findings: docs/reference/settings-menu.md.
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import iop_srsc
import xdb_dump

PROP_TABLE_VA = (
    0x505388  # "User Interface Properties" schema in the ELF: 16-byte entries {name*, type, default*, flags}
)
PROP_COUNT = 316
TEXT_PROP_FIRST, TEXT_PROP_LAST = (
    98,
    142,
)  # contiguous run of "string id" (type 0xa) properties: Main - Load Game .. Load Settings Successful


def iso_files(iso):
    f = open(iso, "rb")

    def rd(lba, n):
        f.seek(lba * 2048)
        return f.read(n)

    root = rd(16, 2048)[156:190]
    out = {}

    def walk(lba, size, path):
        d = rd(lba, size)
        p = 0
        while p < size:
            l = d[p]
            if l == 0:
                p = (p // 2048 + 1) * 2048
                continue
            e = d[p : p + l]
            el = struct.unpack("<I", e[2:6])[0]
            sz = struct.unpack("<I", e[10:14])[0]
            nm = e[33 : 33 + e[32]].decode("latin1").split(";")[0]
            fl = e[25]
            p += l
            if nm in ("\0", "\1"):
                continue
            if fl & 2:
                walk(el, sz, path + nm + "/")
            else:
                out[path + nm] = (el, sz)

    walk(struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0], "/")
    return f, out


def read(f, ent):
    f.seek(ent[0] * 2048)
    return f.read(ent[1])


class Elf:
    def __init__(self, path):
        self.d = open(path, "rb").read()
        (phoff,) = struct.unpack_from("<I", self.d, 0x1C)
        phentsize, phnum = struct.unpack_from("<HH", self.d, 0x2A)
        self.segs = []
        for i in range(phnum):
            t, off, va, pa, fsz, msz = struct.unpack_from("<6I", self.d, phoff + i * phentsize)
            if t == 1:
                self.segs.append((va, off, fsz))

    def off(self, va):
        for v, o, s in self.segs:
            if v <= va < v + s:
                return o + va - v
        raise KeyError(hex(va))

    def u32(self, va):
        return struct.unpack_from("<I", self.d, self.off(va))[0]

    def cstr(self, va):
        o = self.off(va)
        return self.d[o : self.d.index(b"\0", o)].decode("latin1")


def main():
    iso, elfp = sys.argv[1], sys.argv[2]
    f, files = iso_files(iso)
    elf = Elf(elfp)
    names = [elf.cstr(elf.u32(PROP_TABLE_VA + 16 * i)) for i in range(PROP_COUNT)]
    xdb = {
        e["id"]: e["strings"][0].decode("latin1")
        for e in xdb_dump.parse_xdb(read(f, files["/ENGLISH/COMMON/INTERFCE/INTERFCE.XDB"]))
    }
    # the value of property N (N in 98..142) is the u32 at a fixed distance from the first one: anchor on the known first values
    odb = read(f, files["/ENGLISH/COMMON/INTERFCE/INTERFCE.ODB"])
    ents = iop_srsc.parse(odb)[4]
    body = next(odb[o : o + sz] for t, i, s, o, sz in ents if t == 0x102 and i == 1)
    anchor = struct.pack(
        "<5I", 0x75, 0x79, 0x76, 0x77, 0x78
    )  # Main - Load Game, Quit, New Game, Options, Credits -> XDB ids 117, 121, 118, 119, 120
    at = body.index(anchor)  # the property values are packed (not 4-byte aligned inside the object)
    words = struct.unpack_from("<%dI" % ((len(body) - at) // 4), body, at)
    k = 0

    def text_of(prop):
        if TEXT_PROP_FIRST <= prop <= TEXT_PROP_LAST:
            sid = words[k + prop - TEXT_PROP_FIRST]
            return 'XDB %3d "%s"' % (sid, xdb.get(sid, "?"))
        return ""

    rrc = read(f, files["/ENGLISH/DRAKAN2.RRC"])
    for t, i, s, o, sz in iop_srsc.parse(rrc)[4]:
        if t != 0x600:
            continue
        b = rrc[o : o + sz]
        n = struct.unpack_from("<H", b, 0x18)[0]
        print("layout %#06x  flags %d  %d widgets" % (i, struct.unpack_from("<I", b, 0x14)[0], n))
        p = 0x1A
        for _ in range(n):
            wid, prop, _z = struct.unpack_from("<III", b, p)
            typ = struct.unpack_from("<H", b, p + 12)[0]
            x, y, w, h = struct.unpack_from("<4f", b, p + 14)
            print(
                "  widget %#06x  prop %3d %-30s %-34s type %2d  x %.3f y %.3f w %.3f h %.3f"
                % (wid, prop, (names[prop] if 0 < prop < len(names) else "-"), text_of(prop), typ, x, y, w, h)
            )
            p += 0x20


if __name__ == "__main__":
    main()
