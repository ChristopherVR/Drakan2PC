"""Export native bitmap-font cell numbering from INTERFCE.TXD or a 0040_*.bin entry.

Uses only the Python standard library. Outputs geometry, never font pixels.
Coordinates are upright, with y on the marker row; stored_y is its bottom-up equivalent.
"""

import argparse
import csv
import itertools
import json
import struct
from pathlib import Path

import iop_srsc


def scan_font(blob):
    """Read the marker layout used by the PAL font loader (0x285aa8).

    Support the fonts' native 8-bit BGRA palette format, preserving alpha exactly:
    PNG alpha scaling/transparent-pixel cleanup can destroy marker comparisons.
    """
    if len(blob) < 0x450:
        raise ValueError("Truncated font texture")
    w, h, stride, bpp = struct.unpack_from("<4I", blob)
    kind = struct.unpack_from("<I", blob, 0x40)[0]
    if bpp != 8 or kind != 1 or stride != w or not (2 <= w <= 4096 and 2 <= h <= 4096):
        raise ValueError("Expected linear 8-bit font with a 256-entry BGRA palette")
    if len(blob) != 0x450 + w * h:
        raise ValueError("Font texture size mismatch")
    palette = [blob[0x50 + i * 4 : 0x54 + i * 4] for i in range(256)]

    def pixel(x, y):
        return palette[blob[0x450 + (h - 1 - y) * w + x]]

    marker = pixel(0, 0)
    if pixel(0, 1) == marker:
        raise ValueError("Invalid font: marker at (0,1) equals (0,0)")

    def first(values, predicate, label):
        for value in values:
            if predicate(value):
                return value
        raise ValueError(f"Missing {label} marker")

    height_x = first(range(1, w), lambda x: pixel(x, 0) == marker, "height column")
    glyph_height = first(range(1, h), lambda y: pixel(height_x, y) == marker, "height")
    x0 = first(range(height_x + 1, w), lambda x: pixel(x, 0) == marker, "line start")
    space_end = first(range(x0 + 1, w), lambda x: pixel(x, 0) == marker, "space end")
    pitch = first(range(1, h), lambda y: pixel(x0, y) == marker, "line pitch")
    cells, counts = [], []
    for y in range(0, h, pitch):
        if pixel(x0, y) != marker:
            break
        if len(counts) >= 32:
            raise ValueError("Font exceeds loader line capacity")
        start = space_end if y == 0 else x0
        boundaries = [x for x in range(start, w) if pixel(x, y) == marker]
        # The loader temporarily stores the terminal marker, then reuses its slot.
        if len(cells) + len(boundaries) >= 256:
            raise ValueError("Font exceeds loader marker capacity")
        if len(boundaries) < 2 or y + glyph_height > h:
            raise ValueError(f"Invalid glyph line at y={y}")
        counts.append(len(boundaries) - 1)
        for left, right in itertools.pairwise(boundaries):
            cells.append(dict(index=len(cells), x=left, y=y, width=right - left - 1, stored_y=h - 1 - y))
    if not cells:
        raise ValueError("Font has no glyph cells")
    return dict(
        width=w,
        height=h,
        marker_bgra=marker.hex(),
        marker_palette_indices=[i for i, colour in enumerate(palette) if colour == marker],
        height_column=height_x,
        glyph_height=glyph_height,
        line_start_x=x0,
        pitch=pitch,
        space_width=space_end - x0,
        line_counts=counts,
        cell_count=len(cells),
        cells=cells,
    )


def font_entries(data, only=None):
    if data[:4] != b"SRSC":
        yield "font", data
        return
    try:
        _, _, _, _, entries = iop_srsc.parse(data)
    except struct.error as exc:
        raise ValueError("Truncated SRSC index") from exc
    found = False
    for kind, ident, sub, offset, size in entries:
        if kind == 0x40 and ident in ((only,) if only is not None else (96, 97)):
            if offset + size > len(data):
                raise ValueError("Font entry extends beyond container")
            found = True
            yield f"font_{ident}_{sub}", data[offset : offset + size]
    if not found:
        raise ValueError("Requested font entries not found")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path, help="INTERFCE.TXD or raw font override .bin")
    parser.add_argument("output", type=Path, help="Directory for CSV and JSON geometry")
    parser.add_argument("--id", type=int, choices=(96, 97), help="Export only this TXD font")
    args = parser.parse_args()
    try:
        results = [(name, scan_font(blob)) for name, blob in font_entries(args.input.read_bytes(), args.id)]
        args.output.mkdir(parents=True, exist_ok=True)
        for name, result in results:
            (args.output / f"{name}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
            with (args.output / f"{name}.csv").open("w", newline="", encoding="utf-8") as output:
                writer = csv.DictWriter(output, fieldnames=["index", "x", "y", "width", "stored_y"])
                writer.writeheader()
                writer.writerows(result["cells"])
            print(
                f"{name}: {result['cell_count']} cells, height={result['glyph_height']}, "
                f"pitch={result['pitch']}, marker BGRA={result['marker_bgra']}, rows={result['line_counts']}"
            )
    except (OSError, ValueError) as exc:
        parser.exit(1, f"font_cells: {exc}\n")


if __name__ == "__main__":
    main()
