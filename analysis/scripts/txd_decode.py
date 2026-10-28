"""Drakan .TXD (SRSC type 0x40) texture decoder / exporter.

Texture blob layout (reverse engineered, verified on all 97 INTERFCE.TXD textures):
  +0x00 u32 width, +0x04 u32 height, +0x08 u32 stride_bytes, +0x0c u32 bpp (8/16/24/32)
  +0x10 u32 flags/unknown (0,1,2,7,8), +0x14 u32 0xffffffff, ... (rest of the 0x40-byte header unknown)
  bpp 8 : +0x40 palette header {u32 kind, u32 count_or_used, u32 0, u32 0}, then the palette
          (kind 1: 256 x BGRA8888 bytes, alpha 0..0x80 PS2 range; kind 2: 256 x u16 ARGB1555), then width*height index bytes
  bpp 16: +0x40 width*height u16 ARGB1555 (PSMCT16: R bits 0-4, G 5-9, B 10-14, A bit 15)
  bpp 24: +0x40 RGB triplets;  bpp 32: +0x40 RGBA (alpha 0..0x80)
Pixel data is linear (not GS-swizzled): the game uploads it with plain IMAGE transfers.

usage: txd_decode.py FILE.TXD [outdir]      list (and export PNG to outdir)
       txd_decode.py FILE.TXD outdir id     export one
"""

import os
import struct
import sys

sys.path.insert(0, os.path.dirname(__file__))
import iop_srsc

HDR = 0x40


def parse_tex(b):
    w, h, stride, bpp, flags = struct.unpack_from("<5I", b, 0)
    return dict(w=w, h=h, stride=stride, bpp=bpp, flags=flags, size=len(b))


def decode(b):
    """-> (w, h, rgba bytes (alpha scaled 0..255), info dict). Raises ValueError on a layout mismatch."""
    t = parse_tex(b)
    w, h, bpp = t["w"], t["h"], t["bpp"]
    out = bytearray(w * h * 4)

    def a8(a):
        return min(255, a * 2) if a <= 0x80 else 255

    if bpp == 8:
        kind, cnt = struct.unpack_from("<II", b, HDR)
        t["pal_kind"], t["pal_count"] = kind, cnt
        if kind == 1:
            pal = [
                (b[HDR + 18 + i * 4], b[HDR + 17 + i * 4], b[HDR + 16 + i * 4], a8(b[HDR + 19 + i * 4]))
                for i in range(256)
            ]  # stored B,G,R,A
            pix = HDR + 16 + 1024
        elif kind == 2:
            pal = []
            for i in range(256):
                v = struct.unpack_from("<H", b, HDR + 16 + i * 2)[0]
                pal.append(
                    (
                        (v & 31) * 255 // 31,
                        ((v >> 5) & 31) * 255 // 31,
                        ((v >> 10) & 31) * 255 // 31,
                        255 if v & 0x8000 else 0,
                    )
                )
            pix = HDR + 16 + 512
        else:
            raise ValueError("palette kind %d" % kind)
        if pix + w * h != len(b):
            raise ValueError("size mismatch pix=%d len=%d" % (pix + w * h, len(b)))
        t["pix_off"] = pix
        for i in range(w * h):
            out[i * 4 : i * 4 + 4] = bytes(pal[b[pix + i]])
    elif bpp == 16:
        if HDR + w * h * 2 != len(b):
            raise ValueError("size mismatch")
        t["pix_off"] = HDR
        for i in range(w * h):
            v = struct.unpack_from("<H", b, HDR + i * 2)[0]
            out[i * 4 : i * 4 + 4] = bytes(
                (
                    (v & 31) * 255 // 31,
                    ((v >> 5) & 31) * 255 // 31,
                    ((v >> 10) & 31) * 255 // 31,
                    255 if v & 0x8000 else 0,
                )
            )
    elif bpp == 24:
        if HDR + w * h * 3 != len(b):
            raise ValueError("size mismatch")
        t["pix_off"] = HDR
        for i in range(w * h):
            out[i * 4 : i * 4 + 4] = bytes(b[HDR + i * 3 : HDR + i * 3 + 3]) + b"\xff"
    elif bpp == 32:
        if HDR + w * h * 4 != len(b):
            raise ValueError("size mismatch")
        t["pix_off"] = HDR
        for i in range(w * h):
            out[i * 4 : i * 4 + 4] = bytes(b[HDR + i * 4 : HDR + i * 4 + 3]) + bytes([a8(b[HDR + i * 4 + 3])])
    else:
        raise ValueError("bpp %d" % bpp)
    return w, h, bytes(out), t


def write_png(path, w, h, rgba):
    import zlib

    raw = b"".join(b"\0" + rgba[y * w * 4 : (y + 1) * w * 4] for y in range(h))

    def ch(tag, data):
        c = struct.pack(">I", len(data)) + tag + data
        return c + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    open(path, "wb").write(
        b"\x89PNG\r\n\x1a\n"
        + ch(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0))
        + ch(b"IDAT", zlib.compress(raw, 6))
        + ch(b"IEND", b"")
    )


def main():
    d = open(sys.argv[1], "rb").read()
    outdir = sys.argv[2] if len(sys.argv) > 2 else None
    only = int(sys.argv[3]) if len(sys.argv) > 3 else None
    m, v, idx, cnt, ents = iop_srsc.parse(d)
    if outdir:
        os.makedirs(outdir, exist_ok=True)
    for t, i, s, o, sz in ents:
        if t != 0x40 or (only is not None and i != only):
            continue
        try:
            w, h, rgba, info = decode(d[o : o + sz])
        except ValueError as e:
            print("id %3d sub %d size %7d  ERROR %s" % (i, s, sz, e))
            continue
        print(
            "id %3d sub %d %3dx%-3d bpp %2d flags %d pal %s  size %d"
            % (i, s, w, h, info["bpp"], info["flags"], info.get("pal_kind", "-"), sz)
        )
        if outdir:
            write_png(os.path.join(outdir, "tex_%03d.png" % i), w, h, rgba)


if __name__ == "__main__":
    main()
