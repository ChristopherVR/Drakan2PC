"""Pure-python parser/validator for Drakan FMV frames (PS2 IPU 'IDEC' intra macroblock stream).
Frame layout (verified, matches FFmpeg's 'ipu' codec): byte0 = flags (b0-1 intra_dc_precision, b2 dct_type bit present,
b4 alternate_scan, b5 intra_vlc_format, b6 q_scale_type, b7 MPEG-1 coefficients), then per MB: (x||y: '1' = mb_addr_inc)
, mb_type ('1' intra | '01' intra+quant), [dct_type bit], [5-bit qscale], 6 blocks (DC VLC+diff, AC run/level VLCs, EOB),
frame ends with 00 00 01 b0.
usage: vdb_ipuparse.py frame.bin W H [FFmpeg libavcodec/mpeg12data.c]  (prints MB count parsed and trailing bits)"""

import re
import sys


def load_tables(path):
    s = open(path).read()

    def arr(name):
        m = re.search(name + r"[^=]*=\s*\{(.*?)\};", s, re.S)
        return m.group(1)

    def pairs(name):
        return [(int(a, 16), int(b)) for a, b in re.findall(r"\{\s*(0x[0-9a-fA-F]+)\s*,\s*(\d+)\s*\}", arr(name))]

    def ints(name):
        return [int(x) for x in re.findall(r"-?\d+", arr(name))]

    t = {}
    t["mp2"] = pairs("ff_mpeg2_vlc_table")
    t["mp1"] = pairs("ff_mpeg1_vlc_table")
    t["level"] = ints("ff_mpeg12_level")
    t["run"] = ints("ff_mpeg12_run")
    lc = [int(x, 16) for x in re.findall(r"0x[0-9a-f]+", arr("ff_mpeg12_vlc_dc_lum_code"))]
    lb = ints("ff_mpeg12_vlc_dc_lum_bits")
    cc = [int(x, 16) for x in re.findall(r"0x[0-9a-f]+", arr("ff_mpeg12_vlc_dc_chroma_code"))]
    cb = ints("ff_mpeg12_vlc_dc_chroma_bits")
    t["dcl"] = {(c, b): i for i, (c, b) in enumerate(zip(lc, lb))}
    t["dcc"] = {(c, b): i for i, (c, b) in enumerate(zip(cc, cb))}
    return t


class BR:
    def __init__(s, b):
        s.b = b
        s.p = 0

    def get(s, n):
        v = 0
        for _ in range(n):
            v = (v << 1) | ((s.b[s.p >> 3] >> (7 - (s.p & 7))) & 1)
            s.p += 1
        return v

    def left(s):
        return len(s.b) * 8 - s.p


def decode_frame(b, W, H, T):
    br = BR(b)
    flags = br.get(8)
    vlc = T["mp2"] if flags & 0x20 else T["mp1"]
    n = len(T["run"])
    rl = {}
    for i, (c, bits) in enumerate(vlc[:n]):
        rl[(c, bits)] = (T["run"][i], T["level"][i])
    esc = vlc[n]
    eob = vlc[n + 1]

    def dc(tab):
        c = 0
        for bits in range(1, 11):
            c = (c << 1) | br.get(1)
            if (c, bits) in tab:
                sz = tab[(c, bits)]
                if sz == 0:
                    return 0
                return br.get(sz)
        raise ValueError("dc")

    mbs = 0
    for y in range(0, H, 16):
        for x in range(0, W, 16):
            if x or y:
                if not br.get(1):
                    raise ValueError("mbinc at mb %d" % mbs)
            if not br.get(1):
                if not br.get(1):
                    raise ValueError("mbtype")
                q = 1
            else:
                q = 0
            if flags & 4:
                br.get(1)
            if q:
                br.get(5)
            for n6 in range(6):
                dc(T["dcl"] if n6 < 4 else T["dcc"])
                while True:
                    c = 0
                    ok = False
                    for bits in range(1, 17):
                        c = (c << 1) | br.get(1)
                        if (c, bits) == tuple(eob):
                            ok = "eob"
                            break
                        if (c, bits) == tuple(esc):
                            ok = "esc"
                            break
                        if (c, bits) in rl:
                            ok = "rl"
                            break
                    if not ok:
                        raise ValueError("ac vlc at mb %d blk %d bitpos %d" % (mbs, n6, br.p))
                    if ok == "eob":
                        break
                    if ok == "esc":
                        br.get(18)
                    else:
                        br.get(1)
            mbs += 1
    return flags, mbs, br.left()


if __name__ == "__main__":
    T = load_tables(sys.argv[4] if len(sys.argv) > 4 else "build/fmv/mpeg12data.c")
    b = open(sys.argv[1], "rb").read()
    print(decode_frame(b, int(sys.argv[2]), int(sys.argv[3]), T))
