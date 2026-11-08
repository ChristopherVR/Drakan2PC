"""The character preview bake lowers T-pose arms using a synthetic 0x0208 skeleton (no ISO needed)."""

import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "analysis" / "scripts"))
import character_previews as cp


def record(names, origins, children, siblings, weights):
    """One-LOD 0x0208 record; joint matrices are identity rotation with t = -origin (inverse bind)."""
    data = bytearray(64) + struct.pack("<H", 1) + struct.pack("<H", len(names))
    for joint, name in enumerate(names):
        data += name.encode().ljust(32, b"\0") + struct.pack("<i", joint)
    data += struct.pack("<IH", len(weights), len(names))
    for joint, origin in enumerate(origins):
        data += struct.pack("<3f", *(-v for v in origin)) + struct.pack("<9f", 1, 0, 0, 0, 1, 0, 0, 0, 1)
        data += struct.pack("<3i", 0, children[joint], siblings[joint])
        own = [(v, 1.0) for v, j in enumerate(weights) if j == joint]
        data += struct.pack("<H", len(own)) + b"".join(struct.pack("<If", v, w) for v, w in own)
    return bytes(data + struct.pack("<Hf6I", 1, 100.0, 0, 0, 0, len(weights), 0, 0))


class CharacterPreviewPoseTest(unittest.TestCase):
    def test_t_pose_arm_hangs_down(self):
        names = ["root", "Luparm", "Lforearm"]
        origins = [(0, 0, 0), (-0.1, 1.0, 0), (-0.5, 1.0, 0)]
        data = record(names, origins, [1, 2, -1], [0, 0, 0], [0, 1, 2])
        positions = [(0, 0.5, 0), (-0.3, 1.0, 0), (-0.9, 1.0, 0)]
        moved = cp.lower_arms(positions, data)
        self.assertEqual(moved[0], positions[0])  # torso vertex is untouched
        self.assertLess(moved[2][1], 0.3)  # the hand drops below the shoulder
        self.assertGreater(moved[2][0], -0.5)  # and ends up beside the body

    def test_skeleton_without_arms_is_unchanged(self):
        data = record(["root", "wing"], [(0, 0, 0), (-1, 0, 0)], [1, -1], [0, 0], [0, 1])
        positions = [(0, 0, 0), (-1, 0, 0)]
        self.assertEqual(cp.lower_arms(positions, data), positions)


if __name__ == "__main__":
    unittest.main()
