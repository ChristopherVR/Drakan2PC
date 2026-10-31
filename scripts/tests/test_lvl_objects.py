"""Asset-free checks of analysis/scripts/lvl_objects.py on synthetic 0x0000 / 0x0020 payloads built in the test."""

import importlib.util
import math
import struct
import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "analysis" / "scripts"


def load():
    sys.path.insert(0, str(SCRIPTS))
    try:
        spec = importlib.util.spec_from_file_location("lvl_objects", SCRIPTS / "lvl_objects.py")
        mod = importlib.util.module_from_spec(spec)
        sys.modules["lvl_objects"] = mod
        spec.loader.exec_module(mod)
        return mod
    finally:
        sys.path.remove(str(SCRIPTS))


L = load()


def pstr(s, pad=0):
    raw = s.encode() + b"\0" * (1 + pad)
    return struct.pack("<H", len(raw)) + raw


def body(pos, flags=1, events=0, radius=None, links=(), rot=(0, 0, 0), scale=None, data=(), params=()):
    out = struct.pack("<II3fI", 0, 0, *pos, flags) + struct.pack("<H", events)
    if radius is not None:
        out += struct.pack("<f", radius)
    out += struct.pack("<H", len(links)) + b"".join(struct.pack("<I", i) for i in links)
    out += struct.pack("<3h", *rot)
    if scale is not None:
        out += struct.pack("<3f", *scale)
    out += struct.pack("<II", 0, 0xFFFFFFFF)
    out += struct.pack("<II", len(data), len(params)) + b"".join(struct.pack("<I", d) for d in data)
    out += b"".join(struct.pack("<H", p[0]) for p in params)
    for _idx, ftype, name in params:
        out += struct.pack("<I", ftype) + pstr(name)
    return out


def payload(entries):
    """entries: [(id, class_id, db, body bytes)]"""
    out = struct.pack("<H", len(entries))
    for oid, cid, db, _b in entries:
        out += struct.pack("<IHH", oid, cid, db)
    return out + b"".join(e[3] for e in entries)


def header(deps):
    out = pstr("Test") + struct.pack("<III", 512, 512, len(deps))
    for idx, path in deps.items():
        out += struct.pack("<HH", idx, 0) + pstr(path)
    return out


class ObjectStreamTest(unittest.TestCase):
    def test_plain_and_scaled_bodies(self):
        data = payload(
            [
                (2, 100, 8, body((1.0, 2.0, 3.0), rot=(0, 240, 0))),
                (5, 200, 3, body((4.0, 5.0, 6.0), flags=0x101, links=(2, 9), rot=(-89, 90, 0), scale=(1, 2, 3))),
            ]
        )
        objs = L.parse_object_stream(data)
        self.assertEqual(len(objs), 2)
        self.assertEqual(objs[0]["pos"], [1.0, 2.0, 3.0])
        self.assertEqual(L.euler_to_yaw_deg(objs[0]["rot_deg"]), 150)
        self.assertEqual(objs[1]["links"], [2, 9])
        self.assertEqual(objs[1]["rot_deg"], [-89, 90, 0])
        self.assertEqual(objs[1]["scale"], [1.0, 2.0, 3.0])
        self.assertEqual((objs[1]["class_id"], objs[1]["db_index"]), (200, 3))

    def test_radius_word_and_params(self):
        # data area: [enum 1, string (offset 2, 1 dword)], string dword at the tail
        text = struct.unpack("<I", b"Hi\0\0")[0]
        data = payload(
            [
                (
                    1,
                    7,
                    8,
                    body(
                        (0, 0, 0),
                        flags=0x11,
                        radius=6144.0,
                        data=(1, 2 | (1 << 16), text),
                        params=((9, 7, "State"), (10, 0x0A, "Message")),
                    ),
                )
            ]
        )
        (o,) = L.parse_object_stream(data)
        self.assertEqual(o["radius"], 6144.0)
        self.assertEqual([p["name"] for p in o["params"]], ["State", "Message"])
        self.assertEqual(o["params"][0]["value"], 1)
        self.assertEqual(o["params"][1]["value"], "Hi")
        self.assertEqual(o["params"][1]["type_name"], "string")

    def test_truncated_or_trailing_data_is_rejected(self):
        good = payload([(1, 1, 1, body((0, 0, 0)))])
        with self.assertRaises(L.LevelFormatError):
            L.parse_object_stream(good + b"\0")
        with self.assertRaises(L.LevelFormatError):
            L.parse_object_stream(good[:-3])

    def test_bad_sentinel_is_rejected(self):
        bad = bytearray(payload([(1, 1, 1, body((0, 0, 0)))]))
        i = bad.rfind(b"\xff\xff\xff\xff")
        bad[i] = 0
        with self.assertRaises(L.LevelFormatError):
            L.parse_object_stream(bytes(bad))


class HeaderAndRecordsTest(unittest.TestCase):
    def test_header_and_records(self):
        deps = {2: r"..\..\Common\System\System", 8: r"..\Level 1 - Common\Level 1 - Common"}
        hdr = L.parse_level_header(header(deps))
        self.assertEqual(hdr["dependencies"][8], deps[8])
        self.assertEqual(L.odb_name_for(deps[8]), "LEVEL1ON")
        self.assertEqual(L.odb_name_for(deps[2]), "SYSTEM")
        objs = L.parse_object_stream(payload([(3, 77, 8, body((10.0, 20.0, 30.0), rot=(0, 90, 0)))]))
        odbs = {"LEVEL1ON": {77: {"name": "Rail", "group": 5, "runtime_class": 0x100D4}}}
        (rec,) = L.build_records(objs, hdr, odbs)
        self.assertEqual(rec["res_id"], "0x8004d")
        self.assertEqual((rec["name"], rec["runtime_class"], rec["yaw_deg"]), ("Rail", "0x100d4", 0))

    def test_odb_class_id_layouts(self):
        with_xform = bytearray(100)
        struct.pack_into("<H", with_xform, 14, 1)
        struct.pack_into("<I", with_xform, 80, 0x1001C)
        without = bytearray(40)
        struct.pack_into("<I", without, 16, 0x10036)
        self.assertEqual(L.odb_class_id(bytes(with_xform)), 0x1001C)
        self.assertEqual(L.odb_class_id(bytes(without)), 0x10036)

    def test_validate_counts(self):
        objs = L.parse_object_stream(payload([(3, 77, 8, body((10.0, 20.0, 30.0), rot=(0, 90, 0)))]))
        recs = L.build_records(objs)
        world = {"entities": [{"resId": "0x8004d", "pos": [10.0, 20.5, 30.0], "yaw": math.radians(0), "class": "0x1"}]}
        res = L.validate(recs, world)
        self.assertEqual((res["matched_res_id_and_pos"], res["yaw_within_6deg"]), (1, 1))


if __name__ == "__main__":
    unittest.main()
