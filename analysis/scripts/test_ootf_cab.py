"""Synthetic-only tests of the retail cabinet extractor; no original game bytes."""

import struct
import tempfile
import unittest
import zlib
from pathlib import Path

from ootf_cab import extract_cab, inflate_blocks, iso_find, output_path, read_cab


def cabinet(name="file.bin", directory="Data", body=b"example", compressed=False):
    payload = body
    if compressed:
        encoder = zlib.compressobj(wbits=-15)
        block = encoder.compress(body) + encoder.flush()
        payload = struct.pack("<H", len(block)) + block
    data = bytearray(256 + len(payload))
    data[:4] = b"ISc("
    struct.pack_into("<I", data, 12, 64)
    struct.pack_into("<I", data, 64 + 12, 64)
    struct.pack_into("<I", data, 64 + 20, 128)
    struct.pack_into("<I", data, 64 + 28, 1)
    struct.pack_into("<I", data, 64 + 40, 1)
    struct.pack_into("<II", data, 128, 8, 64)
    text = directory.encode() + b"\0"
    data[136 : 136 + len(text)] = text
    text = name.encode() + b"\0"
    data[160 : 160 + len(text)] = text
    struct.pack_into("<IIHII", data, 192, 32, 0, 4 if compressed else 0, len(body), len(payload))
    struct.pack_into("<I", data, 192 + 38, 256)
    data[256:] = payload
    return bytes(data)


class CabinetTests(unittest.TestCase):
    def test_extracts_plain_and_compressed(self):
        for compressed in (False, True):
            with self.subTest(compressed=compressed), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.assertEqual(extract_cab(cabinet(compressed=compressed), root), 1)
                self.assertEqual((root / "Data" / "file.bin").read_bytes(), b"example")

    def test_rejects_traversal_drives_and_windows_special_paths(self):
        with tempfile.TemporaryDirectory() as tmp:
            for name in ("../bad", "..\\bad", "C:\\bad", "/bad", "file:stream", "CON", "bad."):
                with self.subTest(name=name), self.assertRaises(ValueError):
                    output_path(Path(tmp), "Data", name)
            with self.assertRaises(ValueError):
                extract_cab(cabinet(directory="../outside"), Path(tmp))

    def test_rejects_bad_descriptors_and_truncations(self):
        data = cabinet()
        for cut in (0, 15, 64, 128, 200, len(data) - 1):
            with self.subTest(cut=cut), self.assertRaises(ValueError):
                read_cab(data[:cut])
        bad = bytearray(data)
        struct.pack_into("<I", bad, 128 + 4, 127)
        with self.assertRaises(ValueError):
            read_cab(bytes(bad))

    def test_requires_exact_expanded_size(self):
        data = cabinet(compressed=True)
        _, _, _, expanded, size, off = read_cab(data)[0]
        for want in (expanded - 1, expanded + 1):
            with self.subTest(want=want), self.assertRaises(ValueError):
                inflate_blocks(data, off, size, want)
        with self.assertRaises(ValueError):
            inflate_blocks(data, off, size - 1, expanded)

    def test_mdx_padding_is_aligned_to_volume_not_file(self):
        base = 64
        data = bytearray(base + 24 * 2048)
        pvd = base + 16 * 2048
        data[pvd : pvd + 7] = b"\x01CD001\x01"
        struct.pack_into("<I", data, pvd + 158, 20)
        struct.pack_into("<I", data, pvd + 166, 4096)
        # The first directory sector is padding. The record starts at the next *volume* sector.
        at = base + 21 * 2048
        name = b"DATA1.CAB;1"
        data[at] = 44
        struct.pack_into("<I", data, at + 2, 23)
        struct.pack_into("<I", data, at + 10, 3)
        data[at + 32] = len(name)
        data[at + 33 : at + 33 + len(name)] = name
        self.assertEqual(iso_find(bytes(data), base, "DATA1.CAB"), (base + 23 * 2048, 3))


if __name__ == "__main__":
    unittest.main()
