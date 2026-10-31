"""Asset-free checks of analysis/scripts/odb_fields.py on a synthetic class record and field list."""

import importlib.util
import struct
import sys
import unittest
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "analysis" / "scripts"
sys.path.insert(0, str(SCRIPTS))
spec = importlib.util.spec_from_file_location("odb_fields", SCRIPTS / "odb_fields.py")
F = importlib.util.module_from_spec(spec)
spec.loader.exec_module(F)


def pstr(s):
    raw = s.encode() + b"\0"
    raw += b"\0" * (len(raw) % 2)
    return struct.pack("<H", len(raw)) + raw


def plain_class(name, fc, data):
    head = struct.pack("<HHH", 7, 0, 0) + b"\0" * 10 + struct.pack("<HHH", 28, 1, 3)
    return pstr(name) + head + struct.pack("<II", fc, len(data)) + struct.pack("<%dI" % len(data), *data)


class OdbFieldsTest(unittest.TestCase):
    def test_field_lists_and_class_body(self):
        blob = struct.pack("<I", 2) + struct.pack("<I", 1) + pstr("Health") + struct.pack("<I", 0x0A) + pstr("Name")
        self.assertEqual(F.parse_field_lists(blob), [[(1, "Health"), (0x0A, "Name")]])
        data = [35, (1 << 16) | 2, 0x00006968]  # int 35, string at dword 2 ("hi")
        c = F.parse_class_body(plain_class("Orc", 2, data))
        self.assertEqual((c["name"], c["rfl_class"], c["icon"], c["field_count"]), ("Orc", 28, 3, 2))
        self.assertEqual(c["data"], data)

    def test_truncated_blob_and_bad_record_raise(self):
        with self.assertRaises(F.OdbFieldError):
            F.parse_field_lists(struct.pack("<I", 1) + struct.pack("<I", 1))
        bad = plain_class("Orc", 1, [1])[:-2]
        with self.assertRaises(F.OdbFieldError):
            F.parse_class_body(bad)


if __name__ == "__main__":
    unittest.main()
