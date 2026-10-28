# Summarise a DRKGSCAP file (format: docs/reference/gs-capture.md).  usage: python gscap_dump.py file.bin [-v]
import collections
import struct
import sys

REGS = "pmode smode1 smode2 srfsh synch1 synch2 syncv dispfb1 display1 dispfb2 display2 extbuf extdata extwrite bgcolor csr vsyncTick imr busdir siglblid".split()
NREG = {
    0: "PRIM",
    1: "RGBAQ",
    2: "ST",
    3: "UV",
    4: "XYZF2",
    5: "XYZ2",
    6: "TEX0_1",
    7: "TEX0_2",
    8: "CLAMP_1",
    9: "CLAMP_2",
    10: "FOG",
    12: "XYZF3",
    13: "XYZ3",
    0x14: "TEX1_1",
    0x15: "TEX1_2",
    0x16: "TEX2_1",
    0x17: "TEX2_2",
    0x18: "XYOFFSET_1",
    0x19: "XYOFFSET_2",
    0x1A: "PRMODECONT",
    0x1B: "PRMODE",
    0x1C: "TEXCLUT",
    0x22: "SCANMSK",
    0x34: "MIPTBP1_1",
    0x35: "MIPTBP1_2",
    0x36: "MIPTBP2_1",
    0x37: "MIPTBP2_2",
    0x3B: "TEXA",
    0x3D: "FOGCOL",
    0x3F: "TEXFLUSH",
    0x40: "SCISSOR_1",
    0x41: "SCISSOR_2",
    0x42: "ALPHA_1",
    0x43: "ALPHA_2",
    0x44: "DIMX",
    0x45: "DTHE",
    0x46: "COLCLAMP",
    0x47: "TEST_1",
    0x48: "TEST_2",
    0x49: "PABE",
    0x4A: "FBA_1",
    0x4B: "FBA_2",
    0x4C: "FRAME_1",
    0x4D: "FRAME_2",
    0x4E: "ZBUF_1",
    0x4F: "ZBUF_2",
    0x50: "BITBLTBUF",
    0x51: "TRXPOS",
    0x52: "TRXREG",
    0x53: "TRXDIR",
    0x54: "HWREG",
    0x60: "SIGNAL",
    0x61: "FINISH",
    0x62: "LABEL",
}


def parse_gif(d):
    """yield (kind, info) per GIF tag; returns counts"""
    p = 0
    out = []
    while p + 16 <= len(d):
        lo, hi = struct.unpack_from("<QQ", d, p)
        p += 16
        nloop = lo & 0x7FFF
        eop = (lo >> 15) & 1
        pre = (lo >> 46) & 1
        prim = (lo >> 47) & 0x7FF
        flg = (lo >> 58) & 3
        nreg = (lo >> 60) & 0xF or 16
        if flg == 0:
            out.append(("PACKED", nloop, nreg, prim if pre else None))
            p += 16 * nloop * nreg
        elif flg == 1:
            out.append(("REGLIST", nloop, nreg, prim if pre else None))
            p += 8 * ((nloop * nreg + 1) // 2) * 2
        elif flg == 2:
            out.append(("IMAGE", nloop, 0, None))
            p += 16 * nloop
        else:
            out.append(("DISABLED", nloop, 0, None))
        if eop:
            pass
    return out


def main():
    f = open(sys.argv[1], "rb").read()
    verbose = "-v" in sys.argv
    assert f[:8] == b"DRKGSCAP"
    (ver,) = struct.unpack_from("<I", f, 8)
    print("version", ver)
    p = 32
    frame = 0
    stats = collections.Counter()
    pk = collections.Counter()
    regs_last = None
    perframe = collections.defaultdict(lambda: [0, 0])
    while p + 8 <= len(f):
        t, n = struct.unpack_from("<II", f, p)
        p += 8
        pl = f[p : p + n]
        p += n
        if t == 1:
            frame, _ = struct.unpack_from("<QQ", pl)
        elif t == 2:
            path, flags, _, size = struct.unpack_from("<BBHI", pl)
            d = pl[8:]
            perframe[frame][0] += 1
            perframe[frame][1] += size
            pk[(path, flags)] += 1
            for k in parse_gif(d):
                stats[k[0]] += 1
        elif t == 3:
            r = struct.unpack("<20Q", pl)
            if verbose or regs_last is None or r[7] != regs_last[7] or r[0] != regs_last[0]:
                print(
                    f"frame {frame}: PMODE={r[0]:#x} SMODE2={r[2]:#x} DISPFB1={r[7]:#x} DISPLAY1={r[8]:#x} DISPFB2={r[9]:#x} DISPLAY2={r[10]:#x} BGCOLOR={r[14]:#x}"
                )
            regs_last = r
        elif t == 4:
            pass
    print("frames", frame + 1, "packet kinds (path,flags):", dict(pk))
    print("tag kinds", dict(stats))
    busy = [(k, v) for k, v in perframe.items() if v[0]]
    print("frames with packets:", len(busy), "of", frame + 1)
    print("first/last busy frame:", busy[0] if busy else None, busy[-1] if busy else None)


main()
