"""Drakan 2 cover extraction from synthetic title tiles (no disc needed)."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "analysis/scripts"))
import title_cover

TILE = title_cover.TILE


def tile_blob(colour, stored_first_row):
    """A 256x256 24-bit texture: 0x40-byte header, rows stored bottom-up; the first stored row is `stored_first_row`."""
    header = struct.pack("<5I", TILE, TILE, TILE * 3, 24, 0).ljust(0x40, b"\0")
    rows = bytes(stored_first_row) * TILE + bytes(colour) * TILE * (TILE - 1)
    return header + rows


def txd(blobs):
    """An SRSC container with one type-0x40 entry per (id, blob)."""
    body = b"SRSC" + struct.pack("<HIIH", 0x0101, 0, len(blobs), 0)
    entries = []
    for tid, blob in blobs:
        entries.append((0x40, tid, 0, len(body), len(blob)))
        body += blob
    index = len(body)
    body = body[:6] + struct.pack("<I", index) + body[10:]
    return body + b"".join(struct.pack("<HHHII", *e) for e in entries)


def tiles(skip=None):
    return [
        (title_cover.FIRST_TILE_ID + k, tile_blob((k * 40, 10, 20), (255, 255, 255)))
        for k in range(title_cover.COLUMNS * title_cover.ROWS)
        if k != skip
    ]


class TitleCoverTests(unittest.TestCase):
    def test_mosaic_places_tiles_and_flips_rows(self):
        w, h, rows = title_cover.mosaic(txd(tiles()))
        self.assertEqual((w, h), (768, 512))
        # Stored first row is the bottom row of each tile; top rows hold the tile colour.
        self.assertEqual(rows[0][0:3], bytes((0, 10, 20)))
        self.assertEqual(rows[0][256 * 3 : 256 * 3 + 3], bytes((40, 10, 20)))
        self.assertEqual(rows[256][0:3], bytes((120, 10, 20)))
        self.assertEqual(rows[255][0:3], bytes((255, 255, 255)))
        self.assertEqual(rows[511][512 * 3 : 512 * 3 + 3], bytes((255, 255, 255)))

    def test_cover_is_sixteen_by_nine_and_opaque(self):
        w, h, rgba = title_cover.cover(txd(tiles()), 960)
        self.assertEqual((w, h), (960, 540))
        self.assertEqual(len(rgba), 960 * 540 * 4)
        self.assertEqual(set(rgba[3::4]), {255})
        # The bottom copyright strip of the mosaic (rows 432..511) is cropped away.
        self.assertNotIn(255, rgba[0::4][-960:])

    def test_missing_tile_is_an_error(self):
        with self.assertRaises(ValueError):
            title_cover.mosaic(txd(tiles(skip=3)))

    def test_finds_english_interface_txd_case_insensitively(self):
        files = {"/FRENCH/COMMON/INTERFCE/INTERFCE.TXD": (1, 2), "/English/Common/Interfce/Interfce.txd": (3, 4)}
        self.assertEqual(title_cover.find_title_txd(files), (3, 4))
        self.assertIsNone(title_cover.find_title_txd({"/ENGLISH/COMMON/FX/FX.TXD": (1, 2)}))


if __name__ == "__main__":
    unittest.main()
