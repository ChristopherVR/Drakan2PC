"""Parse IOP IRX (ELF32 LE MIPS): .iopmod, import/export tables, sections."""

import glob
import os
import struct
import sys


def parse(path):
    d = open(path, "rb").read()
    shoff = struct.unpack_from("<I", d, 0x20)[0]
    shentsize, shnum, shstrndx = struct.unpack_from("<HHH", d, 0x2E)
    secs = []
    for i in range(shnum):
        n, t, f, a, o, s, l, inf, al, es = struct.unpack_from("<10I", d, shoff + i * shentsize)
        secs.append([n, t, f, a, o, s])
    so = secs[shstrndx][4]

    def nm(n):
        return d[so + n : d.index(b"\0", so + n)].decode()

    for s in secs:
        s.append(nm(s[0]))
    return d, secs


def modinfo(d, secs):
    for n, t, f, a, o, s, name in secs:
        if t == 0x70000080:
            mi, ent, gp, txt, dsz, bsz = struct.unpack_from("<6I", d, o)
            ver = struct.unpack_from("<H", d, o + 24)[0]
            nme = d[o + 26 : o + s].split(bytes(1))[0].decode()
            tsz = txt
            return dict(
                entry=ent, gp=gp, textsz=tsz, datasz=dsz, bsssz=bsz, version="%d.%d" % (ver >> 8, ver & 0xFF), name=nme
            )


def tables(d, secs):
    """scan .text for import (0x41e00000) and export (0x41c00000) records. returns list"""
    imps = []
    exps = []
    for n, t, f, a, o, s, name in secs:
        if t != 1 or not (f & 4):
            continue
        i = o
        while i < o + s - 16:
            w = struct.unpack_from("<I", d, i)[0]
            if w == 0x41E00000 and struct.unpack_from("<I", d, i + 4)[0] == 0:
                ver = struct.unpack_from("<H", d, i + 8)[0]
                lib = d[i + 12 : i + 20].rstrip(b"\0").decode("latin1")
                j = i + 20
                fn = []
                while j < o + s:
                    a0, a1 = struct.unpack_from("<II", d, j)
                    if a0 == 0x03E00008 and (a1 >> 16) == 0x2400:
                        fn.append((a1 & 0xFFFF, j - o + a))
                        j += 8
                    else:
                        break
                imps.append((lib, ver, fn, i - o + a))
                i = j
                continue
            if w == 0x41C00000 and struct.unpack_from("<I", d, i + 4)[0] == 0:
                ver = struct.unpack_from("<H", d, i + 8)[0]
                lib = d[i + 12 : i + 20].rstrip(b"\0").decode("latin1")
                # entries: count to the first 0 after table; sizes in header word at i+? : use .text vaddrs
                j = i + 16 + 4
                ex = []  # skip ver/flags,name(8)? layout: magic,0,ver(2)+flags(2),name[8],then func ptrs
                # name occupies i+12..i+20 ; entries from i+20 until zero terminator
                j = i + 20
                while j < o + s:
                    p = struct.unpack_from("<I", d, j)[0]
                    if p == 0:
                        break
                    ex.append(p)
                    j += 4
                exps.append((lib, ver, ex, i - o + a))
                i = j + 4
                continue
            i += 4
    return imps, exps


if __name__ == "__main__":
    for p in sorted(glob.glob(sys.argv[1] if len(sys.argv) > 1 else "*.IRX")):
        d, secs = parse(p)
        print("==", os.path.basename(p), len(d), "bytes")
        print(" sections:", [(s[6], hex(s[3]), hex(s[5])) for s in secs if s[6]])
        print(" iopmod:", modinfo(d, secs))
        im, ex = tables(d, secs)
        for lib, ver, fn, at in im:
            print("  IMPORT %-8s v%04x @%x: %s" % (lib, ver, at, ",".join(str(x[0]) for x in fn)))
        for lib, ver, e, at in ex:
            print("  EXPORT %-8s v%04x @%x: %d funcs %s" % (lib, ver, at, len(e), [hex(x) for x in e[:40]]))
