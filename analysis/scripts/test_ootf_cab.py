"""Synthetic-only tests of the retail cabinet extractor; no original game bytes."""

import struct
import tempfile
import unittest
import zipfile
import zlib
from pathlib import Path

from ootf_cab import (
    cook_sectors,
    cue_data_track,
    extract_cab,
    inflate_blocks,
    iso_find,
    output_path,
    read_cab,
    read_image,
    sniff_skip,
)


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


CUE = """FILE "Game (Track 1).bin" BINARY
  TRACK 01 MODE1/2352
    INDEX 01 00:00:00
FILE "Game (Track 2).bin" BINARY
  TRACK 02 AUDIO
    INDEX 00 00:00:00
"""


def raw_track(sectors=20, skip=16):
    """A synthetic 2352-byte-sector data track whose sector n holds bytes([n]) * 2048 and a CD001 volume at sector 16."""
    out = bytearray()
    for n in range(sectors):
        user = bytearray([n]) * 2048
        if n == 16:
            user[:6] = b"\x01CD001"
        out += bytes(skip) + bytes(user) + bytes(2352 - skip - 2048)
    return bytes(out)


class RawRipTests(unittest.TestCase):
    def test_cue_names_the_first_data_track(self):
        self.assertEqual(cue_data_track(CUE), ("Game (Track 1).bin", 16))
        self.assertEqual(cue_data_track(CUE.replace("MODE1/2352", "MODE2/2352")), ("Game (Track 1).bin", 24))
        self.assertIsNone(cue_data_track('FILE "a.bin" BINARY\n  TRACK 01 AUDIO\n'))

    def test_cooking_keeps_the_user_data_of_every_sector(self):
        import io

        raw = raw_track()
        cooked = cook_sectors(io.BytesIO(raw), 16)
        self.assertEqual(len(cooked), 20 * 2048)
        self.assertEqual(cooked[5 * 2048 : 5 * 2048 + 3], bytes([5]) * 3)
        self.assertEqual(cooked[16 * 2048 + 1 : 16 * 2048 + 6], b"CD001")
        with self.assertRaises(ValueError):
            cook_sectors(io.BytesIO(raw[:-1]), 16)

    def test_layout_is_sniffed_from_the_volume_descriptor(self):
        self.assertEqual(sniff_skip(raw_track(18, 16)), 16)
        self.assertEqual(sniff_skip(raw_track(18, 24)), 24)
        self.assertIsNone(sniff_skip(bytes(18 * 2352)))

    def test_read_image_handles_a_cue_zip_a_cue_file_and_a_bin(self):
        raw = raw_track()
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            archive = root / "rip.zip"
            with zipfile.ZipFile(archive, "w") as z:
                z.writestr("Game/Game.cue", CUE)
                z.writestr("Game/Game (Track 1).bin", raw)
                z.writestr("Game/Game (Track 2).bin", b"audio")
            expected = read_image(archive)
            self.assertEqual(len(expected), 20 * 2048)
            self.assertEqual(expected[16 * 2048 + 1 : 16 * 2048 + 6], b"CD001")
            (root / "Game (Track 1).bin").write_bytes(raw)
            (root / "Game.cue").write_text(CUE, encoding="utf-8")
            self.assertEqual(read_image(root / "Game.cue"), expected)
            self.assertEqual(read_image(root / "Game (Track 1).bin"), expected)
            (root / "audio.bin").write_bytes(b"\0" * 40000)
            with self.assertRaises(ValueError):
                read_image(root / "audio.bin")
            bad = root / "bad.zip"
            with zipfile.ZipFile(bad, "w") as z:
                z.writestr("Game.cue", CUE)
            with self.assertRaises(ValueError):
                read_image(bad)


if __name__ == "__main__":
    unittest.main()
