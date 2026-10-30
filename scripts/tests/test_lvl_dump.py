"""Asset-free checks of analysis/scripts/lvl_dump.py and odb_dump.py on synthetic SRSC data built in the test."""

import importlib.util
import json
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

SCRIPTS = Path(__file__).resolve().parents[2] / "analysis" / "scripts"
CLASS_ID = 0x17081069


def load_modules():
    """Load lvl_dump and odb_dump by path. Both import sibling modules, so the scripts dir stays on sys.path."""
    sys.path.insert(0, str(SCRIPTS))
    try:
        mods = {}
        for name in ("lvl_dump", "odb_dump"):
            spec = importlib.util.spec_from_file_location(name, SCRIPTS / (name + ".py"))
            module = importlib.util.module_from_spec(spec)
            sys.modules[name] = module
            spec.loader.exec_module(module)
            mods[name] = module
        return mods["lvl_dump"], mods["odb_dump"]
    finally:
        sys.path.remove(str(SCRIPTS))


LVL, ODB = load_modules()


def make_srsc(entries):
    """entries: (type, id, sub, payload). Layout: 16-byte header, payloads back to back, index at the end."""
    body = bytearray()
    index = []
    for typ, rid, sub, payload in entries:
        index.append((typ, rid, sub, 16 + len(body), len(payload)))
        body += payload
    idx_off = 16 + len(body)
    out = struct.pack("<4sHII", b"SRSC", 0x0101, idx_off, len(entries)) + b"\0\0" + bytes(body)
    for typ, rid, sub, off, size in index:
        out += struct.pack("<HHHII", typ, rid, sub, off, size)
    return out


def name_field(text, width):
    """u16 length (padded width) followed by the NUL-padded name, as in the 0x101/0x102 records."""
    return struct.pack("<H", width) + text.encode("latin-1").ljust(width, b"\0")


class SrscParseTests(unittest.TestCase):
    def test_round_trip_and_ranges(self):
        blob = make_srsc([(0x0001, 7, 3, b"abcd"), (0x0020, 9, 0, b"xy")])
        ents = LVL.parse_srsc(blob)
        self.assertEqual([(e.type, e.id, e.sub, e.size) for e in ents], [(1, 7, 3, 4), (0x20, 9, 0, 2)])
        self.assertEqual(blob[ents[0].offset : ents[0].offset + ents[0].size], b"abcd")

    def test_rejects_bad_magic_and_truncated_index(self):
        blob = make_srsc([(1, 1, 0, b"abcd")])
        with self.assertRaises(ValueError):
            LVL.parse_srsc(b"XXXX" + blob[4:])
        with self.assertRaises(ValueError):
            LVL.parse_srsc(blob[:-3])

    def test_inflate_only_complete_zlib_streams(self):
        packed = zlib.compress(b"level data " * 20)
        self.assertEqual(LVL.try_inflate(packed), b"level data " * 20)
        self.assertIsNone(LVL.try_inflate(packed[:-4]))
        self.assertIsNone(LVL.try_inflate(b"\x00\x01\x02\x03\x04\x05\x06\x07"))


class HeuristicTests(unittest.TestCase):
    def test_float_runs_skip_small_ints_and_out_of_range(self):
        payload = struct.pack("<4I", 1, 2, 3, 4) + struct.pack("<6f", 1.5, 2.25, -3.0, 0.5, 4.0, -8.0)
        payload += struct.pack("<3f", 1e6, 1e6, 1e6)
        runs = LVL.float_runs(payload)
        self.assertEqual(runs["run_count"], 1)
        self.assertEqual((runs["top_runs"][0]["offset"], runs["top_runs"][0]["count"]), (16, 6))

    def test_stride_finds_record_period(self):
        # 40 records of 32 bytes: a constant class word at the head of each, the rest varies per record.
        recs = b"".join(
            struct.pack("<II", CLASS_ID, i + 1)
            + struct.pack("<6I", *[(i * 7 + k) * 2654435761 & 0xFFFFFFFF or 1 for k in range(6)])
            for i in range(40)
        )
        cands = LVL.stride_candidates(recs)["candidates"]
        self.assertTrue(cands)
        self.assertEqual(cands[0]["stride_bytes"], 32)
        self.assertGreater(cands[0]["score"], 1.0)

    def test_stride_ignores_zero_padding(self):
        self.assertEqual(LVL.stride_candidates(bytes(4096))["candidates"], [])

    def test_strings_flag_length_prefix(self):
        b = struct.pack("<H", 6) + b"Aachen" + b"\0\0" + b"\x00\x00\x00\x00\x00"
        s = LVL.ascii_strings(b)
        self.assertEqual(s["count"], 1)
        self.assertEqual(s["first"][0]["text"], "Aachen")
        self.assertTrue(s["first"][0]["len_prefixed"])

    def test_repeated_u32_reports_known_class_id(self):
        b = struct.pack("<I", CLASS_ID) * 4 + struct.pack("<I", 5)
        rep = LVL.repeated_u32(b)
        self.assertEqual(rep["top"][0]["value"], "0x%08x" % CLASS_ID)
        self.assertEqual(rep["top"][0]["count"], 4)
        self.assertEqual(rep["known_present"][0]["count"], 4)


class DumpLevelTests(unittest.TestCase):
    def test_dump_writes_index_bins_and_analysis(self):
        packed = zlib.compress(struct.pack("<8f", *[1.0 + i for i in range(8)]))
        odb_like = make_srsc([(0x0102, 3, 9, b"raw-instance"), (0x0103, 0, 0, packed)])
        broken = b"NOTSRSC" + bytes(40)
        with tempfile.TemporaryDirectory() as tmp:
            index = LVL.dump_level("TESTLVL", [("TEST.ODB", odb_like), ("BAD.LVL", broken)], Path(tmp), analyse=True)
            level_dir = Path(tmp) / "TESTLVL"
            self.assertEqual(index["entry_count"], 2)
            self.assertEqual([s["file"] for s in index["skipped"]], ["BAD.LVL"])
            by_type = {e["type"]: e for e in index["entries"]}
            inst = by_type["0x0102"]
            self.assertFalse(inst["zlib"])
            self.assertEqual((level_dir / inst["raw"]).read_bytes(), b"raw-instance")
            comp = by_type["0x0103"]
            self.assertTrue(comp["zlib"])
            self.assertEqual(comp["inflated_size"], 32)
            self.assertEqual(
                (level_dir / comp["inflated"]).read_bytes(), struct.pack("<8f", *[1.0 + i for i in range(8)])
            )
            self.assertEqual(index["histogram"]["0x0103"]["zlib"], 1)
            doc = json.loads((level_dir / "analysis.json").read_text(encoding="utf-8"))
            self.assertEqual(len(doc["entries"]), 2)
            comp_a = next(e for e in doc["entries"] if e["type"] == "0x0103")
            self.assertEqual(comp_a["source"], "inflated")
            self.assertGreater(comp_a["floats"]["run_count"], 0)


def odb_fixture():
    """A three-class ODB: class 0x29 'Decor' with six instances, class 5 with one, and a zlib schema blob."""
    entries = [(0x0100, 0, 0, struct.pack("<I", 0x14))]
    for cid, name in ((0x29, "Decor"), (0x05, "Level Properties")):
        entries.append((0x0101, cid, 0, name_field(name, 18)))
    for k in range(6):
        body = struct.pack("<I", 0x29 | (k << 16)) + struct.pack("<3I", 0, 0, 0x10000)
        body += struct.pack("<3f", 0.5 + 0.1 * k, 0.2 * k, 1.0 - 0.3 * k) + struct.pack("<I", 0)
        entries.append((0x0102, 40 + k, 0x29, name_field("Crate %d" % k, 10) + body))
    entries.append((0x0102, 90, 0x05, name_field("Level Properties", 18) + struct.pack("<I", 5) + bytes(8)))
    schema = struct.pack("<I", 2) + struct.pack("<IH", 0xF, 4) + b"ab\0\0" + struct.pack("<IH", 0x2, 5) + b"hello"
    entries.append((0x0103, 0, 0, zlib.compress(schema)))
    return make_srsc(entries)


class OdbDumpTests(unittest.TestCase):
    def test_load_classes_instances_and_schema(self):
        odb = ODB.load_odb(odb_fixture())
        self.assertEqual(odb["classes"], {0x29: "Decor", 0x05: "Level Properties"})
        self.assertEqual(len(odb["instances"]), 7)
        self.assertEqual(odb["instances"][0]["name"], "Crate 0")
        self.assertEqual(odb["schema"]["parsed"], 2)
        self.assertTrue(odb["schema"]["complete"])
        self.assertEqual(odb["schema"]["first_names"], ["ab", "hello"])

    def test_schema_stops_at_unparsable_bytes(self):
        blob = struct.pack("<I", 3) + struct.pack("<IH", 0x1, 3) + b"abc" + b"\x01\x02"
        sch = ODB.parse_schema(blob)
        self.assertEqual((sch["parsed"], sch["declared"]), (1, 3))
        self.assertFalse(sch["complete"])

    def test_analyse_roles_per_class(self):
        res = ODB.analyse(odb_fixture(), words=8)
        self.assertEqual(res["class_ref_match"], 7)
        self.assertEqual(res["sizes"]["by_class"][0]["sub"], 0x29)
        block = res["fields_by_class"]["0x29"]
        self.assertEqual(block["class"], "Decor")
        roles = {f["word"]: f["role"] for f in block["fields"]}
        self.assertEqual(roles[0], "class-ref-low16")
        self.assertEqual(roles[1], "const")
        self.assertEqual(roles[3], "const")
        self.assertEqual(roles[4], "float")
        self.assertIn(16, [t["offset"] for t in block["float_triples"]])
        self.assertNotIn("0x05", res["fields_by_class"])  # one record: below the per-class minimum


if __name__ == "__main__":
    unittest.main()
