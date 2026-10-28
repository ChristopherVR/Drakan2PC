"""CPU-only decoder fixtures and numeric RT-filter counterexamples.

The counterexamples model current shader math; they do not execute HLSL or
prove that a particular game pixel uses the affected sampling configuration.
"""

import struct
import tempfile
import unittest
from pathlib import Path

from gs_rt_capture_audit import audit


def fbits(x):
    return struct.unpack("<I", struct.pack("<f", x))[0]


def packet(entries, packed=True):
    descs = sum(reg << (4 * i) for i, (reg, _, _) in enumerate(entries))
    tag = len(entries) << 60 | (0 if packed else 1 << 58) | 1
    body = b"".join(struct.pack("<QQ", lo, hi) if packed else struct.pack("<Q", lo) for _, lo, hi in entries)
    if not packed and len(entries) & 1:
        body += b"\0" * 8
    return struct.pack("<QQ", tag, descs) + body


class Decoder(unittest.TestCase):
    def decode(self, geometry):
        regs = [(0, 0x316), (0x4D, 465 | 2 << 16), (7, 14880 | 2 << 14 | 7 << 26 | 7 << 30), (9, 5)]
        setup = b"".join(packet([(14, value, address)]) for address, value in regs)
        payload = struct.pack("<BBHI", 3, 0, 0, len(setup + geometry)) + setup + geometry
        data = b"DRKGSCAP" + struct.pack("<I", 1) + b"\0" * 20 + struct.pack("<II", 2, len(payload)) + payload
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fixture.bin"
            path.write_bytes(data)
            return audit(path)

    def test_packed_uv_and_xyz(self):
        geometry = packet(
            [(3, 40 | 2040 << 32, 0), (1, 127 | 127 << 32, 127 | 140 << 32), (5, 32 | 64 << 32, 0x80000000)]
        )
        result = self.decode(geometry)
        sample, prod = result["samples"][0], result["producers"][0]
        self.assertEqual(sample["uv_min"], [2.5, 127.5])
        self.assertEqual(prod["xy_min"], [2, 4])
        self.assertEqual(prod["z_min"], 0x80000000)
        self.assertEqual(prod["rgba_min"], [127, 127, 127, 140])
        self.assertEqual(sample["observed_frame_candidates"], [{"fbp": 465, "psm": 0, "origin": [0, 0]}])

    def test_reglist_uv_and_xyz(self):
        result = self.decode(packet([(3, 40 | 2040 << 16, 0), (5, 32 | 64 << 16 | 55 << 32, 0)], False))
        self.assertEqual(result["samples"][0]["uv_min"], [2.5, 127.5])
        self.assertEqual(result["producers"][0]["xy_min"], [2, 4])
        self.assertEqual(result["producers"][0]["z_min"], 55)

    def test_packed_stq(self):
        geometry = packet([(14, 0x216, 0), (2, fbits(0.5) | fbits(0.25) << 32, fbits(2)), (5, 0, 0)])
        result = self.decode(geometry)
        self.assertEqual(result["samples"][0]["uv_min"], [32, 16])
        self.assertFalse(result["samples"][0]["fst"])


class SamplingCounterexamples(unittest.TestCase):
    def test_repeat_at_logical_edge(self):
        # Logical two-texel RT region [red,black] within a larger allocation.
        # u=0: GS wraps taps [-1,0] -> [black,red]; current hardware clamp
        # sees physical taps [-1,0] -> [red,red]. Different seam, same centre.
        logical = [255, 0]
        expected = (logical[-1 % 2] + logical[0]) / 2
        current = logical[0]
        self.assertEqual(expected, 127.5)
        self.assertEqual(current, 255)
        self.assertNotEqual(expected, current)

    def test_aem_before_filter(self):
        # CT24 AEM/TA0=128: red has alpha128, black alpha0.
        rgb = (255 + 0) / 2
        correct_alpha = (128 + 0) / 2
        post_filter_alpha = 128 if rgb != 0 else 0
        correct_rgb = (rgb * correct_alpha / 128, 200 * (1 - correct_alpha / 128))
        current_rgb = (rgb * post_filter_alpha / 128, 200 * (1 - post_filter_alpha / 128))
        self.assertEqual(correct_rgb, (63.75, 100))
        self.assertEqual(current_rgb, (127.5, 0))
        self.assertNotEqual(correct_rgb, current_rgb)


if __name__ == "__main__":
    unittest.main()
