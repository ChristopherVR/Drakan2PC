import struct
import sys

sys.stdout.reconfigure(errors="replace")
D = open(__import__("pathlib").Path(__file__).resolve().parents[2] / "iso" / "SCES_500.06", "rb").read()


def rd(va, n):
    return D[0x1000 + va - 0x100000 : 0x1000 + va - 0x100000 + n]


def u32(va):
    return struct.unpack("<I", rd(va, 4))[0]


def cstr(va):
    b = rd(va, 80)
    return b.split(b"\0")[0].decode("latin1")


def findptr(target):
    out = []
    t = struct.pack("<I", target)
    i = D.find(t, 0x1000)
    while i >= 0:
        if (i - 0x1000) % 4 == 0:
            out.append(0x100000 + i - 0x1000)
        i = D.find(t, i + 1)
    return out
