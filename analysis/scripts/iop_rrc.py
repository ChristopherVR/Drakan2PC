"""Decrypt (+inflate) a Surreal .RRC file as the EE does (func 0x1e5668 + inflate).
usage: iop_rrc.py ELF file.rrc out.bin"""

import sys
import zlib

ELF_DEFAULT = r"iso/SCES_500.06"
elf = open(sys.argv[1] if len(sys.argv) > 1 and sys.argv[1].upper().endswith(".06") else ELF_DEFAULT, "rb").read()


def foff(va):
    return va - 0x100000 + 0x1000


key0 = bytearray(elf[foff(0x4E9A78) : foff(0x4E9A78) + 0x25])


def mutate(k):
    k = bytearray(k)
    a0 = 0x17
    for i in range(0x25):
        k[i] = (k[i] + a0 + (a0 >> 8)) & 255
        a0 += 0x17
    return k


def decrypt(buf, key, seed):
    buf = bytearray(buf)
    n = len(buf) // 2
    t1 = seed % 0x25
    t0 = seed & 0xFF
    p = 0
    for _ in range(n):
        k1 = key[t1]
        t1 += 1
        if t1 >= 0x25:
            t1 = 0
        t3 = buf[p] ^ (k1 ^ t0)
        t0 = t0 - 1
        if t0 < 0:
            t0 = 0xFF
        k2 = key[t1]
        a0 = buf[p + 1] ^ (k2 ^ t0)
        a3 = t0 - 1
        nt1 = t1 + 1
        t1 = nt1 if nt1 < 0x25 else 0
        t0 = a3 if a3 > -1 else 0xFF
        if (k1 >> 3) & 1:
            buf[p] = t3
            buf[p + 1] = a0
        else:
            buf[p] = a0
            buf[p + 1] = t3
        p += 2
    return bytes(buf)


if __name__ == "__main__":
    d = open(sys.argv[2], "rb").read()
    for name, k in (("raw", key0), ("mut", mutate(key0))):
        o = decrypt(d, k, len(d))
        print(name, o[:16].hex())
        try:
            r = zlib.decompressobj().decompress(o)
            print(" inflated", len(r))
            open(sys.argv[3], "wb").write(r)
            break
        except Exception as e:
            print(" fail", e)
