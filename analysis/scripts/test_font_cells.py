"""Synthetic marker layouts; optional local-disc regression via DRAKAN_FONT_TXD."""

import os
import struct
import unittest
from pathlib import Path

from font_cells import font_entries, scan_font


def fixture():
    w, h = 24, 18
    blob = bytearray(0x450 + w * h)
    struct.pack_into("<4I", blob, 0, w, h, w, 8)
    struct.pack_into("<I", blob, 0x40, 1)
    blob[0x50 + 7 * 4 : 0x54 + 7 * 4] = b"\x01\xff\xff\x00"
    # A second palette index with the SAME colour must count as a marker too.
    blob[0x50 + 8 * 4 : 0x54 + 8 * 4] = b"\x01\xff\xff\x00"

    def mark(x, y, index=7):
        blob[0x450 + (h - 1 - y) * w + x] = index

    for x in (0, 1, 3, 7, 12, 18):
        mark(x, 0)
    mark(1, 4)  # vertical height, unrelated to the x coordinate
    for x in (3, 8, 14, 20):
        mark(x, 6, 8)
    # No line at y=12: unrelated markers elsewhere must not create cells.
    mark(10, 12)
    return blob


class FontCellsTest(unittest.TestCase):
    def test_vertical_calibration_and_terminal_markers(self):
        result = scan_font(fixture())
        self.assertEqual((result["glyph_height"], result["pitch"], result["space_width"]), (4, 6, 4))
        self.assertEqual(result["marker_palette_indices"], [7, 8])
        self.assertEqual(result["line_counts"], [2, 3])
        self.assertEqual(
            [(c["index"], c["x"], c["y"], c["width"], c["stored_y"]) for c in result["cells"]],
            [(0, 7, 0, 4, 17), (1, 12, 0, 5, 17), (2, 3, 6, 4, 11), (3, 8, 6, 5, 11), (4, 14, 6, 5, 11)],
        )

    def test_invalid_layouts(self):
        for blob in (b"", fixture()[:-1], bytearray(len(fixture()))):
            with self.subTest(size=len(blob)), self.assertRaises(ValueError):
                scan_font(blob)
        blob = fixture()
        blob[0x450 + 16 * 24] = 7
        with self.assertRaisesRegex(ValueError, "marker at"):
            scan_font(blob)
        blob = fixture()
        blob[0x450 + 13 * 24 + 1] = 0
        with self.assertRaisesRegex(ValueError, "height marker"):
            scan_font(blob)

    def test_container_and_raw_entry(self):
        blob = fixture()
        self.assertEqual(list(font_entries(blob)), [("font", blob)])
        data = bytearray(16)
        data.extend(blob)
        index = len(data)
        data.extend(struct.pack("<HHHII", 0x40, 97, 0, 16, len(blob)))
        struct.pack_into("<4sHII", data, 0, b"SRSC", 0x101, index, 1)
        self.assertEqual(list(font_entries(data)), [("font_97_0", blob)])
        with self.assertRaisesRegex(ValueError, "not found"):
            list(font_entries(data, 96))
        with self.assertRaisesRegex(ValueError, "Truncated SRSC"):
            list(font_entries(data[:-1]))

    @unittest.skipUnless(os.environ.get("DRAKAN_FONT_TXD"), "requires local INTERFCE.TXD")
    def test_original_fonts(self):
        results = {
            name: scan_font(blob) for name, blob in font_entries(Path(os.environ["DRAKAN_FONT_TXD"]).read_bytes())
        }
        for name, total, height, pitch, first_x in (("font_96_0", 216, 15, 21, 16), ("font_97_0", 224, 17, 22, 9)):
            font = results[name]
            self.assertEqual((font["cell_count"], font["glyph_height"], font["pitch"]), (total, height, pitch))
            self.assertEqual(font["cells"][0]["x"], first_x)
            self.assertEqual(font["marker_bgra"], "01ffff00")
        # Independent anchor: the four direction icons occupy the final stored row 35.
        self.assertEqual(
            [(c["x"], c["stored_y"]) for c in results["font_97_0"]["cells"][-4:]],
            [(3, 35), (26, 35), (49, 35), (70, 35)],
        )


if __name__ == "__main__":
    unittest.main()
