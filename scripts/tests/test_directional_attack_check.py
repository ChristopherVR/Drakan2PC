import copy
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import directional_attack_check as directional


def fixture():
    samples = {}

    def sample(animation=0, position=0, time=0):
        return {
            "weapon": 42,
            "hp": 100,
            "animation": 0,
            "rynn": {"position": [position, 0, 0]},
            "pose": {
                "attacking": bool(animation),
                "right_weapon_ref": 42,
                "right_holder_mode": 0,
                "left_holder_mode": 0xFFFFFFFF,
                "upper": {"clip": animation, "time": time},
                "lower": {"clip": animation, "time": time},
            },
        }

    for label, animation in (("neutral_neutral", 10), ("up_arrow", 20), ("up_wasd", 20)):
        for phase in ("base", "move", "released", "idle"):
            samples[f"ram_{label}_{phase}"] = sample()
        for offset in directional.ATTACK_FRAMES:
            samples[f"ram_{label}_attack_{offset:02}"] = sample(animation, time=offset / 30)
    for phase, position in (("before", 0), ("after", 100), ("idle1", 100), ("idle2", 100)):
        samples[f"ram_movement_{phase}"] = sample(position=position)
    return samples


class DirectionalAttackCheckTest(unittest.TestCase):
    def test_native_parity(self):
        checks, _ = directional.evaluate(fixture(), ["up"])
        self.assertTrue(all(checks.values()), checks)

    def test_locomotion_is_not_attack_identity(self):
        samples = fixture()
        for offset in directional.ATTACK_FRAMES:
            samples[f"ram_up_wasd_attack_{offset:02}"]["pose"]["lower"]["clip"] = 99
        checks, _ = directional.evaluate(samples, ["up"])
        self.assertTrue(checks["up_same_native_move"])
        for offset in directional.ATTACK_FRAMES:
            samples[f"ram_up_wasd_attack_{offset:02}"]["pose"]["upper"]["clip"] = 30
        checks, _ = directional.evaluate(samples, ["up"])
        self.assertFalse(checks["up_same_native_move"])

    def test_native_node_channel_reader(self):
        image = bytearray(0x2000000)
        put = lambda address, value: struct.pack_into("<I", image, address, value)
        context, handle, inner, player, skeleton, channels, table = (0x600000 + i * 0x1000 for i in range(7))
        for address, value in (
            (0x5565D4, context),
            (context + 0x4B0, handle),
            (handle + 0xFC, inner),
            (inner + 0x10, player),
            (handle + 0x50, skeleton),
            (skeleton + 0xE8, channels),
            (skeleton + 0xF0, 2),
            (skeleton + 0xDC, table),
            (player + 0x94, 0x80),
            (channels + 0x50, 100),
            (channels + 0x80 + 0x50, 200),
        ):
            put(address, value)
        struct.pack_into("<H", image, player + 0xED0, 42)
        struct.pack_into("<h", image, table + 4, 1)  # node 2 maps to channel 1
        struct.pack_into("<f", image, channels + 0x80 + 0x58, 0.5)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "pose.ram"
            path.write_bytes(image)
            pose = directional.native_pose(path)
            self.assertTrue(pose["attacking"])
            self.assertEqual(pose["right_weapon_ref"], 42)
            self.assertEqual(pose["lower"]["clip"], 100)
            self.assertEqual(pose["upper"], {"clip": 200, "time": 0.5})
            put(skeleton + 0xF0, 33)
            path.write_bytes(image)
            with self.assertRaisesRegex(ValueError, "channel count"):
                directional.native_pose(path)

    def test_ordinary_attack_cannot_pass(self):
        samples = fixture()
        for control in ("arrow", "wasd"):
            for offset in directional.ATTACK_FRAMES:
                samples[f"ram_up_{control}_attack_{offset:02}"]["pose"]["upper"]["clip"] = 10
        checks, _ = directional.evaluate(samples, ["up"])
        self.assertFalse(checks["up_directional_baseline"])
        self.assertFalse(checks["up_same_native_move"])

    def test_idle_and_missing_samples_fail(self):
        samples = fixture()
        for offset in directional.ATTACK_FRAMES:
            samples[f"ram_up_wasd_attack_{offset:02}"]["pose"]["attacking"] = False
        checks, _ = directional.evaluate(samples, ["up"])
        self.assertFalse(checks["up_wasd_native_attack"])
        samples.pop("ram_up_arrow_move")
        checks, _ = directional.evaluate(samples, ["up"])
        self.assertFalse(checks["required_samples"])

    def test_release_and_movement_failures(self):
        samples = fixture()
        samples["ram_up_wasd_released"]["pose"]["attacking"] = True
        samples["ram_movement_after"] = copy.deepcopy(samples["ram_movement_before"])
        checks, _ = directional.evaluate(samples, ["up"])
        self.assertFalse(checks["up_wasd_released"])
        self.assertFalse(checks["movement_after_attack"])

    def test_recipe_releases_before_idle_and_covers_controls(self):
        lines = directional.recipe(["up"]).splitlines()
        self.assertEqual(lines, sorted(lines, key=lambda line: int(line.split()[0])))
        self.assertIn("242 key down UP", lines)
        self.assertIn("382 key down W", lines)
        self.assertEqual(sum("button down 1" in line for line in lines), 3)
        self.assertEqual(sum("button up 1" in line for line in lines), 3)


if __name__ == "__main__":
    unittest.main()
