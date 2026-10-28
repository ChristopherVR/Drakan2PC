"""Asset-free CPU characterization; these tests do not execute the renderer."""

import struct
import unittest

from gs_hud_edge_audit import collect, events, point_footprint, rectangle_border


def packed(entries):
    descs = sum(reg << (4 * i) for i, (reg, _, _) in enumerate(entries))
    return struct.pack("<QQ", len(entries) << 60 | 1, descs) + b"".join(
        struct.pack("<QQ", value, high) for _, value, high in entries
    )


def ad(reg, value):
    return packed([(14, value, reg)])


def capture(packet):
    payload = struct.pack("<BBHI", 3, 0, 0, len(packet)) + packet
    return b"DRKGSCAP" + struct.pack("<I", 1) + bytes(20) + struct.pack("<II", 2, len(payload)) + payload


def fbits(value):
    return struct.unpack("<I", struct.pack("<f", value))[0]


class PacketEvidence(unittest.TestCase):
    def test_complete_upload_excludes_qword_padding(self):
        raw = bytes([255, 255, 255, 127]) * 6
        packet = ad(0x50, 15008 << 32 | 2 << 48) + ad(0x52, 3 | 2 << 32) + ad(0x53, 0)
        packet += struct.pack("<QQ", 2 << 58 | 2, 0) + raw + bytes(8)
        upload = collect(capture(packet), 15008)["uploads"][0]
        self.assertTrue(upload["complete"])
        self.assertEqual(upload["raw"], raw)
        self.assertEqual(upload["received_bytes"], 32)

    def test_partial_upload_is_not_pixel_proof(self):
        packet = ad(0x50, 15008 << 32 | 2 << 48) + ad(0x52, 128 | 128 << 32) + ad(0x53, 0)
        packet += struct.pack("<QQ", 2 << 58 | 1, 0) + bytes(16)
        self.assertFalse(collect(capture(packet), 15008)["uploads"][0]["complete"])

    def test_stq_and_xyoffset(self):
        t0 = 15008 | 2 << 14 | 7 << 26 | 7 << 30 | 1 << 34
        packet = ad(0, 0x25E) + ad(6 + 1, t0) + ad(0x18 + 1, 32768 | 32768 << 32)
        for x, y, u, v in [(13, 436, 28.5, 60.5), (73, 494, 88.5, 2.5)]:
            packet += packed(
                [
                    (2, fbits(u / 128) | fbits(v / 128) << 32, fbits(1)),
                    (1, 127 | 127 << 32, 127 | 128 << 32),
                    (5, int((x + 2048) * 16) | int((y + 2048) * 16) << 32, 0x7FFFFFFF),
                ]
            )
        result = collect(capture(packet), 15008)
        vertices = result["sprites"][0]["vertices"]
        self.assertEqual([v["xy"] for v in vertices], [[13, 436], [73, 494]])
        self.assertEqual([v["uv"] for v in vertices], [[28.5, 60.5], [88.5, 2.5]])
        self.assertEqual(vertices[0]["rgba"], [127, 127, 127, 128])

    def test_reglist_uv_and_xyz(self):
        packet = ad(0, 0x156) + ad(6, 15008 | 7 << 26 | 7 << 30)
        entries = [(3, 456 | 968 << 16), (5, 208 | 6976 << 16), (3, 1416 | 40 << 16), (5, 1168 | 7904 << 16)]
        packet += struct.pack("<QQ", 4 << 60 | 1 << 58 | 1, 0x5353)
        packet += b"".join(struct.pack("<Q", value) for _, value in entries)
        vertices = collect(capture(packet), 15008)["sprites"][0]["vertices"]
        self.assertEqual([v["uv"] for v in vertices], [[28.5, 60.5], [88.5, 2.5]])
        self.assertEqual([v["xy"] for v in vertices], [[13, 436], [73, 494]])

    def test_adc_suppresses_second_sprite_kick(self):
        packet = ad(0, 0x116) + ad(6, 15008)
        packet += packed([(5, 0, 0), (5, 16 | 16 << 32, 1 << 47)])
        self.assertEqual(collect(capture(packet), 15008)["sprites"], [])

    def test_truncated_record_and_split_tag_rejected(self):
        with self.assertRaisesRegex(ValueError, "Truncated record"):
            list(events(capture(b"")[:-1]))
        with self.assertRaisesRegex(ValueError, "Split GIF tag payload"):
            list(events(capture(struct.pack("<QQ", 1 << 60 | 2, 0xE))))

    def test_disabled_tag_is_not_image(self):
        packet = struct.pack("<QQ", 3 << 58 | 1, 0) + bytes(16)
        self.assertEqual(list(events(capture(packet))), [])


class AtlasFootprint(unittest.TestCase):
    @staticmethod
    def atlas():
        # Original image bytes are deliberately not embedded in this fixture.
        # Only the measured rectangle geometry and constant RGBA are synthetic.
        raw = bytearray([255, 255, 255, 0] * (128 * 128))
        for y in range(2, 62):
            for x in range(27, 89):
                if x in (27, 88) or y in (2, 61):
                    raw[(y * 128 + x) * 4 + 3] = 127
        return raw

    def test_atlas_border_is_real_content(self):
        self.assertEqual(rectangle_border(self.atlas(), 128, 27, 2, 88, 61), {(255, 255, 255, 127)})

    def test_current_scale4_point_footprint_makes_bottom_right_L(self):
        xs = point_footprint(13, 73, 28.5, 88.5, 4)
        ys = point_footprint(436, 494, 60.5, 2.5, 4)
        self.assertEqual((len(xs), len(ys)), (240, 232))
        self.assertEqual((xs.count(88), ys.count(2)), (2, 2))
        self.assertNotIn(27, xs)
        self.assertNotIn(61, ys)
        raw = self.atlas()
        self.assertEqual(sum(raw[(y * 128 + x) * 4 + 3] == 127 for y in ys for x in xs), 940)

    def test_scale1_has_right_border_under_current_math(self):
        self.assertEqual(point_footprint(13, 73, 28.5, 88.5, 1)[-1], 88)
        self.assertEqual(point_footprint(436, 494, 60.5, 2.5, 1)[-1], 3)

    def test_shifted_footprint_is_only_a_counterexample(self):
        # A half-pixel UV correction would remove the box, but this alone does
        # not establish which footprint the real GS or original UI intended.
        raw = self.atlas()
        xs = point_footprint(13, 73, 28, 88, 4)
        ys = point_footprint(436, 494, 61, 3, 4)
        self.assertEqual(sum(raw[(y * 128 + x) * 4 + 3] for y in ys for x in xs), 0)

    def test_reversed_and_fractional_screen_coverage(self):
        self.assertEqual(point_footprint(0, 2, 2, 0, 2), [1, 1, 0, 0])
        self.assertEqual(len(point_footprint(0.625, 2, 0, 2, 1)), 1)


if __name__ == "__main__":
    unittest.main()
