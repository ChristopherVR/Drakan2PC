"""Write the Drakan 2 cover picture (the title screen artwork) from the player's own PS2 disc.

usage: python -I title_cover.py --iso <game.iso> --out previews/game-d2.png [--width 1280]

The title menu backdrop is six 256x256 24-bit textures (ids 72..77) of ENGLISH/COMMON/INTERFCE/INTERFCE.TXD: a 3x2 mosaic of
768x512 pixels, each tile stored bottom-up. The top 768x432 (16:9) holds the logo, the heroine and the dragon; the copyright
lines at the bottom are left out. The crop is scaled up to --width (default 1280x720) with a bilinear filter. Private derivative of
the supplied disc: written under the build output, never tracked. Standard library only.
"""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

# The kit's embeddable Python (._pth) does not put the script's folder on sys.path.
sys.path.insert(0, str(Path(__file__).resolve().parent))

import iop_srsc
import txd_decode
from ui_layout_dump import iso_files, read

TILE = 256
FIRST_TILE_ID = 72
COLUMNS, ROWS = 3, 2
CROP_W, CROP_H = COLUMNS * TILE, 432  # 16:9 out of the 768x512 mosaic


def find_title_txd(files):
    """The English INTERFCE.TXD entry of an iso_files() map, or None."""
    for name in sorted(files):
        upper = name.upper()
        if upper.startswith("/ENGLISH/") and upper.endswith("/INTERFCE/INTERFCE.TXD"):
            return files[name]
    return None


def mosaic(txd):
    """Decode the six title tiles of a TXD into one upright RGB mosaic -> (w, h, rows of bytes)."""
    _magic, _version, _index, _count, entries = iop_srsc.parse(txd)
    wanted = {FIRST_TILE_ID + k: k for k in range(COLUMNS * ROWS)}
    rows = [bytearray() for _ in range(ROWS * TILE)]
    seen = set()
    for kind, tid, _sub, offset, size in entries:
        if kind != 0x40 or tid not in wanted:
            continue
        w, h, rgba, _info = txd_decode.decode(txd[offset : offset + size])
        if (w, h) != (TILE, TILE):
            raise ValueError("title tile %d is %dx%d" % (tid, w, h))
        col, row = wanted[tid] % COLUMNS, wanted[tid] // COLUMNS
        seen.add(tid)
        for y in range(TILE):
            src = (TILE - 1 - y) * TILE * 4  # stored bottom-up
            line = rgba[src : src + TILE * 4]
            dst = rows[row * TILE + y]
            if len(dst) < (col + 1) * TILE * 3:
                dst.extend(bytes(((col + 1) * TILE * 3) - len(dst)))
            dst[col * TILE * 3 : (col + 1) * TILE * 3] = b"".join(line[i : i + 3] for i in range(0, TILE * 4, 4))
    if seen != set(wanted):
        raise ValueError("title tiles missing: %s" % sorted(set(wanted) - seen))
    return COLUMNS * TILE, ROWS * TILE, [bytes(r) for r in rows]


def scale(rows, src_w, src_h, dst_w, dst_h):
    """Bilinear resize of RGB rows -> RGBA bytes (opaque)."""
    out = bytearray()
    xs = []
    for x in range(dst_w):
        fx = (x + 0.5) * src_w / dst_w - 0.5
        x0 = max(0, min(src_w - 1, int(fx // 1)))
        x1 = min(src_w - 1, x0 + 1)
        xs.append((x0 * 3, x1 * 3, min(1.0, max(0.0, fx - x0))))
    for y in range(dst_h):
        fy = (y + 0.5) * src_h / dst_h - 0.5
        y0 = max(0, min(src_h - 1, int(fy // 1)))
        y1 = min(src_h - 1, y0 + 1)
        wy = min(1.0, max(0.0, fy - y0))
        a, b = rows[y0], rows[y1]
        for x0, x1, wx in xs:
            for c in range(3):
                top = a[x0 + c] + (a[x1 + c] - a[x0 + c]) * wx
                bot = b[x0 + c] + (b[x1 + c] - b[x0 + c]) * wx
                out.append(int(top + (bot - top) * wy + 0.5))
            out.append(255)
    return bytes(out)


def cover(txd, width=1280):
    """-> (w, h, rgba) of the 16:9 cover."""
    w, _h, rows = mosaic(txd)
    height = width * 9 // 16
    return width, height, scale(rows[:CROP_H], w, CROP_H, width, height)


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--iso", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--width", type=int, default=1280, help="output width (16:9; default 1280)")
    args = ap.parse_args(argv)
    if args.width < CROP_W or args.width % 16:
        ap.error("--width must be a multiple of 16 and at least %d" % CROP_W)
    f, files = iso_files(str(args.iso))
    with f:
        entry = find_title_txd(files)
        if entry is None:
            print("title_cover: no INTERFCE.TXD on this disc; no cover written", file=sys.stderr)
            return 0
        txd = read(f, entry)
    try:
        w, h, rgba = cover(txd, args.width)
    except ValueError as error:
        print("title_cover: %s; no cover written" % error, file=sys.stderr)
        return 0
    args.out.parent.mkdir(parents=True, exist_ok=True)
    txd_decode.write_png(str(args.out), w, h, rgba)
    print("title_cover: wrote %s (%dx%d)" % (args.out, w, h))
    return 0


if __name__ == "__main__":
    sys.exit(main())
