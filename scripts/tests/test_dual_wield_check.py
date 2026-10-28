import struct
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import dual_wield_check as dw

RIGHT, LEFT, MODEL = 0xA6B5B0, 0xA7D4B0, 0x430000


class Image:
    """A synthetic 32 MiB EE RAM image with just the structures the check reads."""

    def __init__(self):
        self.m = bytearray(32 * 1024 * 1024)

    def u32(self, a, v):
        struct.pack_into("<I", self.m, a, v & 0xFFFFFFFF)

    def u16(self, a, v):
        struct.pack_into("<H", self.m, a, v)

    def text(self, a, s):
        self.m[a : a + len(s) + 1] = s.encode() + b"\0"


def scene(left_equipped=True, attached=True, owner=4, line="In left hand", selection=16, bow=False):
    im = Image()
    ctx, player, sk, reg, db = 0x400000, 0x420000, 0x440000, 0x450000, 0x460000
    im.u32(dw.CONTEXT, ctx)
    im.u32(ctx + 0x4B0, 0x410000)
    im.u32(0x410000 + 0xFC, 0x410100)
    im.u32(0x410100 + 0x10, player)
    im.u32(player + 0x80, MODEL)
    im.u16(MODEL + 0x12, 4)
    im.u32(MODEL + 0x50, sk)
    # registry: ref 1 = right weapon, ref 2 = left weapon
    im.u32(ctx + 0x7C, reg)
    im.u32(ctx + 0x84, 10)
    im.u32(reg, RIGHT)
    im.u32(reg + 8, LEFT)
    # resource db: index 5 = class 0x1000e
    im.u32(ctx + 0x320, db)
    im.u32(db + 0x10, 100)
    im.u32(db + 8, 0x461000)
    im.u32(0x461000 + 5 * 0x24 + 8, dw.CLASS_MELEE)
    # skeleton: bone 19 -> record 0 -> attachment 0 -> ref 2 (left weapon)
    im.u32(sk + 8, 8)
    im.u32(sk + 0xDC, 0x442000)
    im.u32(sk + 0xE8, 0x441000)
    im.u32(sk + 0xF4, 0x443000)
    im.u32(0x441000 + 4, 19)
    im.u32(0x441000 + 8, 0 if attached else 0xFFFFFFFF)
    im.u16(0x443000 + 4, 2)
    for i, (h, data, cell, eq) in enumerate(((RIGHT, 0x470000, 1, True), (LEFT, 0x480000, 16, left_equipped))):
        im.u32(h + 0xC, dw.TYPE_WEAPON)
        im.u16(h + 0x10, 5)
        im.u32(h + 0x54, h + 0x200)
        im.u32(h + 0x204, h + 0x300)
        im.u32(h + 0x300 + dw.PROP_STANCE * 4, 0)
        im.u32(h + 0xFC, h + 0x100)
        im.u32(h + 0x110, data)
        rec = player + dw.CTL + dw.CTL_SLOTS + cell * 0x10
        im.u32(rec, 1 if eq else 0)
        im.u32(rec + 8, h)
    im.u16(0x480000 + dw.WEAPON_OWNER, owner)
    im.u32(player + dw.PLAYER_SLOTS, RIGHT)
    im.u32(player + dw.PLAYER_SLOTS + 2 * dw.SLOT_STRIDE, LEFT if left_equipped else 0)
    im.u32(player + dw.LEFT_HOLDER, dw.BOW_MODE if bow else 0)
    ctl = player + dw.CTL
    im.u32(ctl + dw.CTL_SELECTION, selection)
    im.text(ctl + dw.CTL_NAME, "Rynn's Dagger")
    im.text(ctl + dw.CTL_HAND_LINE, line)
    return bytes(im.m)


def trace(
    left_weight=1.0, grips_error=0.0, wrong_blade=False, two_lefts=False, wrong_hit=False, left_hit=True, root_skew=0.0
):
    lines = []

    def pose(clip, step, left, w):
        lines.append(f"[dual-wield] pose clip={clip:x} t=0.1 step={step} left={int(left)} armed=1 weight={w:.2f}")

    for _ in range(5):
        pose(0x20691, 1, False, 0.0)
    for w in (0.14, 0.5, left_weight):
        pose(0x2068F, 2, True, w)
        lines.append(
            f"[dual-wield] grips w={w:.2f} native L(-163 100 -228) R(314 191 6) -> "
            f"L({-314 + grips_error:.0f} 191 6) R(163 100 -228)"
        )
    if two_lefts:
        pose(0x20690, 3, True, 1.0)
    pose(0x20691, 1, False, 0.0)
    blade = "[dual-wield] blade right={} left={} strike={}"
    lines += [blade.format(0, 0, "right"), blade.format(1, 0, "right"), blade.format(0, 0, "right")]
    lines += [blade.format(0, 0, "left"), blade.format(0, 1, "left")]
    if wrong_blade:  # the right blade live through a left strike
        lines += [blade.format(1, 1, "left"), blade.format(1, 0, "left")]
    lines += [blade.format(0, 0, "left"), blade.format(0, 0, "right")]
    lines.append("[dual-wield] hit hand=right strike=right targets=1")
    if left_hit:
        lines.append("[dual-wield] hit hand=left strike=left targets=1")
    if wrong_hit:
        lines.append("[dual-wield] hit hand=left strike=right targets=2")
    lines.append(f"[dual-wield] root w=1.00 world (3.0 0.0 -4.0) -> (-3.0 0.0 {-4.0 - root_skew:.1f})")
    return "\n".join(lines)


class CursorTest(unittest.TestCase):
    def test_measured_cells(self):
        self.assertEqual(dw.cell_cursor(1), (104, 128))
        self.assertEqual(dw.cell_cursor(16), (55, 235))
        with self.assertRaises(ValueError):
            dw.cell_cursor(24)


class IniTest(unittest.TestCase):
    def test_patch(self):
        text = "; c\n[general]\ndual_wield = 0\nlanguage = english\n[video]\n; mode\nwindow_mode = borderless\n"
        out = dw.patch_ini(text, {"general": {"dual_wield": "1", "inventory_screen": "native"}, "video": dw.WINDOW})
        self.assertIn("dual_wield = 1", out)
        self.assertIn("language = english", out)
        self.assertIn("window_mode = windowed", out)
        self.assertIn("; mode", out)
        self.assertEqual(out.count("dual_wield"), 1)
        general = out.split("[video]")[0]
        self.assertIn("inventory_screen = native", general)  # appended to its own section
        self.assertIn("aspect = 4:3", out.split("[video]")[1])

    def test_new_section(self):
        out = dw.patch_ini("", {"general": {"dual_wield": "1"}})
        self.assertEqual(out, "[general]\ndual_wield = 1\n")


class StateTest(unittest.TestCase):
    def test_read(self):
        st = dw.read_state(scene())
        self.assertEqual((st.right, st.shield_slot, st.on_left_bone), (RIGHT, LEFT, LEFT))
        self.assertEqual((st.left_owner, st.model_ref), (4, 4))
        self.assertEqual((st.selection, st.hand_line, st.selected_name), (16, "In left hand", "Rynn's Dagger"))
        self.assertEqual([(w.cell, w.equipped, w.one_handed) for w in st.weapons], [(1, True, True), (16, True, True)])

    def test_pick(self):
        st = dw.read_state(scene(left_equipped=False))
        self.assertEqual(dw.pick_cells(st), (1, 16))
        with self.assertRaises(SystemExit):
            dw.pick_cells(dw.read_state(scene()))  # shield slot taken
        with self.assertRaises(SystemExit):
            dw.pick_cells(dw.read_state(scene(left_equipped=False, bow=True)))

    def test_checks(self):
        states = {
            "ram_03_left_hover": dw.read_state(scene(left_equipped=False, line="Equips to left hand")),
            "ram_04_left_equipped": dw.read_state(scene()),
            "ram_05_unequipped": dw.read_state(scene(left_equipped=False, attached=False, line="Equips to left hand")),
        }
        res = dw.check_states(states, 1, 16)
        for name in ("details", "equip", "attach", "unequip"):
            self.assertTrue(res[name]["ok"], name)
        bad = dict(states, ram_04_left_equipped=dw.read_state(scene(owner=0)))
        self.assertFalse(dw.check_states(bad, 1, 16)["attach"]["ok"])
        bad = dict(states, ram_05_unequipped=dw.read_state(scene()))
        self.assertFalse(dw.check_states(bad, 1, 16)["unequip"]["ok"])
        self.assertFalse(dw.check_states({}, 1, 16)["equip"]["ok"])


class TraceTest(unittest.TestCase):
    def test_pass(self):
        r = dw.analyze_trace(trace())
        for name in ("strikes", "mirror", "blade", "hits", "root"):
            self.assertTrue(r[name]["ok"], (name, r[name]))
        self.assertEqual(r["strikes"]["sequence"], ["R1:0.00", "L2:1.00", "R1:0.00"])
        self.assertEqual(r["mirror"]["frames_checked"], 1)

    def test_wrong_blade_for_one_observation_at_a_strike_change(self):
        # The previous strike's blade can show once after the switch; it goes dead right after.
        text = trace().replace(
            "blade right=0 left=1 strike=left",
            "blade right=0 left=1 strike=left\n[dual-wield] blade right=0 left=1 strike=right",
        )
        self.assertTrue(dw.analyze_trace(text)["blade"]["ok"])

    def test_blend_out_grips_not_checked(self):
        # After a strike the mirror blends out while no pose line is traced; those grips sit between the poses.
        blend_out = "[dual-wield] grips w=0.40 native L(-163 100 -228) R(314 191 6) -> L(-100 150 -60) R(90 120 -150)"
        r = dw.analyze_trace(trace() + "\n" + blend_out)
        self.assertTrue(r["mirror"]["ok"], r["mirror"])

    def test_failures(self):
        self.assertFalse(dw.analyze_trace(trace(left_weight=0.6))["strikes"]["ok"])
        self.assertFalse(dw.analyze_trace(trace(two_lefts=True))["strikes"]["ok"])
        self.assertFalse(dw.analyze_trace(trace(grips_error=40))["mirror"]["ok"])
        self.assertFalse(dw.analyze_trace(trace(wrong_blade=True))["blade"]["ok"])
        self.assertFalse(dw.analyze_trace(trace(wrong_hit=True))["hits"]["ok"])
        self.assertFalse(dw.analyze_trace(trace(left_hit=False))["hits"]["ok"])
        self.assertFalse(dw.analyze_trace(trace(root_skew=3.0))["root"]["ok"])
        self.assertFalse(dw.analyze_trace("")["strikes"]["ok"])


class RecipeTest(unittest.TestCase):
    def test_recipe(self):
        text = dw.check_recipe(1, 16, right_held=False)
        self.assertIn("cursor 104 128", text)
        self.assertIn("cursor 55 235", text)
        self.assertEqual(text.count("button down 1"), 6)
        self.assertEqual(text.count("button up 1"), 6)
        self.assertTrue(text.startswith("clock milliseconds"))
        # events run in file order: every phase (between syncs) is sorted by time
        times = []
        for line in text.splitlines()[1:]:
            if " sync " in line:
                self.assertEqual(times, sorted(times))
                times = []
            else:
                times.append(int(line.split()[0]))
        self.assertEqual(times, sorted(times))
        self.assertIn("dump ram_05_unequipped", text)
        self.assertTrue(text.rstrip().endswith("quit"))
        self.assertNotIn("key down F", text)
        engaged = dw.check_recipe(1, 16, right_held=False, engage=True)
        self.assertIn("300 key down F", engaged)
        self.assertIn("dump ram_05_fight", engaged)
        self.assertNotIn("ram_05_unequipped", engaged)
        self.assertTrue(engaged.rstrip().endswith("quit"))
        self.assertEqual(dw.check_recipe(1, 16, False, taps=10).count("button down 1"), 10)
        held = dw.check_recipe(1, 16, right_held=True)
        self.assertNotIn("ram_01_right_hover", held)
        self.assertIn("ram_04_left_equipped", held)


if __name__ == "__main__":
    unittest.main()
