"""The log parser and the checks of scripts/harness/puzzle_door_check.py on synthetic watch lines."""

import importlib.util
import sys
import unittest
from pathlib import Path

SPEC = importlib.util.spec_from_file_location(
    "puzzle_door_check", Path(__file__).resolve().parents[1] / "harness" / "puzzle_door_check.py"
)
puz = importlib.util.module_from_spec(SPEC)
sys.modules["puzzle_door_check"] = puz  # dataclasses look the module up by name
SPEC.loader.exec_module(puz)


def door(t: float, yaw: float, words: str = "") -> str:
    return (
        f"[watch {t:.2f}] ref=3351 h=00a02ce0 cls=10013 type=8 pos=651312,147485,966406 yaw={yaw:.3f} | e+ | d+{words}"
    )


def rynn(t: float, z: int) -> str:
    return f"[watch {t:.2f}] ref=22 h=00b00000 cls=1001a type=1 pos=652600,147498,{z} yaw=3.142 | e+"


class ParseTest(unittest.TestCase):
    def test_open_and_locked(self):
        r = puz.parse("\n".join([door(1, 0.0, "10=0 40=1"), door(2, 1.7, "10=1")]))
        self.assertTrue(puz.opened(r))
        self.assertFalse(puz.locked(r))  # the last line has no Locked word
        r = puz.parse(door(1, 0.0, "40=1 44=5"))
        self.assertFalse(puz.opened(r))
        self.assertTrue(puz.locked(r))

    def test_walk_checks(self):
        blocked = puz.parse("\n".join(rynn(i, 963000 + i * 1000) for i in range(3)) + "\n" + rynn(9, 966030))
        self.assertTrue(puz._walk(True)(blocked)[0])
        self.assertFalse(puz._walk(False)(blocked)[0])
        through = puz.parse(rynn(1, 966030) + "\n" + rynn(2, 972301))
        self.assertTrue(puz._walk(False)(through)[0])

    def test_toggle_and_messages(self):
        r = puz.parse("\n".join(door(i, y) for i, y in enumerate([0, 1.7, 1.7, 0.014])))
        self.assertTrue(puz._closed_after_open(r)[0])
        r = puz.parse("[fn 396428 t1] ra=1 a0=2 a1=20 a2=3 [a3]=ffff1003,1,2,3")
        self.assertIn(("396428", 0x1003), r.messages)

    def test_ambush_moves(self):
        cre = "[watch {:.2f}] ref=3353 h=0 cls=10031 type=7 pos={},147485,{} yaw=1.571 | e+"
        lines = [cre.format(i, 652512, 965606) for i in range(6)] + [cre.format(7, 651700, 963000)]
        self.assertTrue(puz._ambush(puz.parse("\n".join(lines)))[0])
        still = [cre.format(i, 652512, 965606) for i in range(8)]
        self.assertFalse(puz._ambush(puz.parse("\n".join(still)))[0])


if __name__ == "__main__":
    unittest.main()
