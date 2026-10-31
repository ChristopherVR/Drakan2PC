"""Asset-free checks of scripts/savetool/savetool.py on synthetic SRSC saves built inside each test."""

import contextlib
import io
import json
import math
import struct
import sys
import tempfile
import unittest
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "savetool"))
import savetool as st

TYPE_REC = 0x102
RYNN = st.RYNN_CLASS
GOLD = st.GOLD_CLASS
SKILL_NAMES = (b"Melee", b"Archery", b"Magic")


def record(class_id, size, patches):
    """A 0x102 object payload: u32 +0, u32 +4, class id at +8, then (offset -> bytes) patches."""
    data = bytearray(size)
    struct.pack_into("<II", data, 0, 0xA50, 0)
    struct.pack_into("<I", data, 8, class_id)
    for off, blob in patches.items():
        data[off : off + len(blob)] = blob
    return bytes(data)


def skill_table():
    """Count u32 at 0x334, then three 76-byte rows {id, level, max, name[64]} starting at 0x338."""
    data = bytearray(0x338 + 3 * 76 + 16)
    struct.pack_into("<I", data, 0x334, 3)
    for k, name in enumerate(SKILL_NAMES):
        row = 0x338 + k * 76
        struct.pack_into("<III", data, row, k + 1, 1, 8)
        data[row + 12 : row + 12 + len(name)] = name
    return bytes(data)


def make_srsc(entries, *, level=6, gap=b"", compress=True):
    """Independent SRSC writer. entries are (type, id, sub, payload); gap bytes sit before each payload."""
    body = bytearray(16)
    index = []
    for typ, eid, sub, payload in entries:
        body += gap
        off = len(body)
        raw = zlib.compress(payload, level) if compress else payload
        body += raw
        index.append((typ, eid, sub, off, len(raw)))
    index_off = len(body)
    for row in index:
        body += struct.pack("<HHHII", *row)
    struct.pack_into("<4sHIIH", body, 0, b"SRSC", 0x0101, index_off, len(entries), 0)
    return bytes(body)


def sample_entries(rynn_id=56):
    rynn = record(RYNN, 0x720, {0x708: struct.pack("<I", 84), 12: struct.pack("<3f", 100.5, -20.25, 3000.0)})
    gold = record(GOLD, 0x460, {0x448: struct.pack("<I", 376)})
    camera = record(0x1234, 64, {12: struct.pack("<3f", 1.0, 2.0, 3.0)})
    return [
        (0x400, 0, 0, b"\x32\x00" + b"Levels/Level 1A - Surdana/Level 1A - Surdana.lvl"),
        (TYPE_REC, rynn_id, 0, rynn),
        (TYPE_REC, 63, 0, gold),
        (TYPE_REC, 70, 0, camera),
        (st.TYPE_SKILLS, st.SKILL_TABLE_ID, 0, skill_table()),
        (0x104, 9, 0, bytes(range(40))),
    ]


def sample_save(rynn_id=56, level=6):
    return make_srsc(sample_entries(rynn_id), level=level, gap=b"\xaa" * 3)


def run(argv):
    out = io.StringIO()
    err = io.StringIO()
    with contextlib.redirect_stdout(out), contextlib.redirect_stderr(err):
        code = st.main(argv)
    return code, out.getvalue(), err.getvalue()


class SaveToolTest(unittest.TestCase):
    def setUp(self):
        self._tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self._tmp.name)

    def tearDown(self):
        self._tmp.cleanup()

    def write(self, name, blob):
        path = self.dir / name
        path.write_bytes(blob)
        return path

    def test_parse_and_assemble_are_byte_identical(self):
        for level in (1, 6, 9):
            blob = sample_save(level=level)
            self.assertEqual(st.assemble(st.parse(blob))[0], blob, f"level {level}")

    def test_dump_then_pack_is_byte_identical(self):
        for level in (1, 6):
            src = self.write(f"s{level}.rsg", sample_save(level=level))
            out_dir = self.dir / f"dump{level}"
            self.assertEqual(run(["dump", str(src), str(out_dir)])[0], 0)
            repacked = self.dir / f"packed{level}.rsg"
            self.assertEqual(run(["pack", str(out_dir), "-o", str(repacked)])[0], 0)
            self.assertEqual(repacked.read_bytes(), src.read_bytes())

    def test_dump_keeps_original_compressed_bytes(self):
        # Level 1 streams carry a different FLG byte from zlib's default, so recompression cannot reproduce them.
        blob = sample_save(level=1)
        src = self.write("l1.rsg", blob)
        out_dir = self.dir / "dump"
        run(["dump", str(src), str(out_dir)])
        manifest = json.loads((out_dir / st.MANIFEST).read_text(encoding="utf-8"))
        stored = (out_dir / manifest["entries"][0]["raw_file"]).read_bytes()
        self.assertNotEqual(zlib.compress(zlib.decompress(stored)), stored)
        self.assertEqual(stored, st.parse(blob).entries[0].raw)

    def test_info_lists_entries(self):
        code, text, _ = run(["info", str(self.write("i.rsg", sample_save()))])
        self.assertEqual(code, 0)
        self.assertIn("6 entries", text)
        self.assertIn("0x00401", text)
        self.assertIn("skill table", text)
        self.assertIn("class 0x17081069", text)

    def test_get_reads_each_field(self):
        save = st.parse(sample_save())
        self.assertEqual(st.get_field(save, "hp"), 84)
        self.assertEqual(st.get_field(save, "gold"), 376)
        self.assertEqual(st.get_field(save, "pos"), (100.5, -20.25, 3000.0))
        self.assertEqual(st.get_field(save, "skill.1"), 1)
        self.assertEqual(st.get_field(save, "skill.1.max"), 8)
        self.assertEqual(st.get_field(save, "skill.3"), 1)

    def test_cli_get_prints_value(self):
        src = self.write("g.rsg", sample_save())
        self.assertEqual(run(["get", str(src), "gold"])[1], "376\n")
        self.assertEqual(run(["get", str(src), "pos"])[1], "100.5,-20.25,3000\n")

    def test_set_changes_only_the_target_bytes(self):
        save = st.parse(sample_save())
        before = {e.index: e.data for e in save.entries}
        st.set_field(save, "pos", "1,2,3")
        st.set_field(save, "skill.2", "3")
        after = st.parse(st.assemble(save)[0])
        self.assertEqual(st.get_field(after, "pos"), (1.0, 2.0, 3.0))
        self.assertEqual(st.get_field(after, "skill.2"), 3)
        changed = {e.index for e in after.entries if e.data != before[e.index]}
        rynn = next(e for e in after.entries if e.type == TYPE_REC and e.id == 56)
        skills = next(e for e in after.entries if e.type == st.TYPE_SKILLS)
        self.assertEqual(changed, {rynn.index, skills.index})
        old = before[rynn.index]
        self.assertEqual(rynn.data[:12], old[:12])
        self.assertEqual(rynn.data[24:], old[24:])
        self.assertEqual(rynn.data[0x708:0x70C], old[0x708:0x70C])
        self.assertEqual(
            [i for i in range(len(old)) if old[i] != rynn.data[i]],
            [i for i in range(12, 24) if old[i] != rynn.data[i]],
        )

    def test_untouched_entries_keep_their_raw_bytes(self):
        save = st.parse(sample_save(level=1))
        raw_before = [e.raw for e in save.entries]
        st.set_field(save, "gold", "9999")
        changed = [i for i, (a, b) in enumerate(zip(raw_before, [e.raw for e in save.entries], strict=True)) if a != b]
        self.assertEqual(len(changed), 1)
        self.assertEqual(st.get_field(st.parse(st.assemble(save)[0]), "gold"), 9999)

    def test_cli_set_writes_out_and_keeps_input(self):
        src = self.write("in.rsg", sample_save())
        original = src.read_bytes()
        out = self.dir / "out.rsg"
        code, text, _ = run(["set", str(src), "gold", "9999", "-o", str(out)])
        self.assertEqual(code, 0)
        self.assertIn("376 -> 9999", text)
        self.assertEqual(src.read_bytes(), original)
        self.assertEqual(run(["get", str(out), "gold"])[1], "9999\n")
        self.assertFalse((self.dir / "out.rsg.bak").exists())

    def test_cli_set_keeps_bak_of_existing_output(self):
        src = self.write("in.rsg", sample_save())
        out = self.write("out.rsg", b"previous")
        run(["set", str(src), "hp", "37", "-o", str(out)])
        self.assertEqual((self.dir / "out.rsg.bak").read_bytes(), b"previous")
        first_set = out.read_bytes()
        run(["set", str(src), "hp", "38", "-o", str(out)])
        self.assertEqual((self.dir / "out.rsg.bak.1").read_bytes(), first_set)
        self.assertEqual(run(["get", str(out), "hp"])[1], "38\n")

    def test_in_place_requires_flag_and_keeps_bak(self):
        src = self.write("p.rsg", sample_save())
        original = src.read_bytes()
        self.assertEqual(run(["set", str(src), "hp", "37", "-o", str(src)])[0], 1)
        self.assertEqual(src.read_bytes(), original)
        self.assertEqual(run(["set", str(src), "hp", "37", "--in-place"])[0], 0)
        self.assertEqual((self.dir / "p.rsg.bak").read_bytes(), original)
        self.assertEqual(run(["get", str(src), "hp"])[1], "37\n")

    def test_bounds_are_enforced(self):
        save = st.parse(sample_save())
        cases = [
            ("skill.1", "9"),  # above max 8
            ("skill.1.max", "0"),  # below level 1
            ("gold", "-1"),
            ("gold", str(2**32)),
            ("pos", "1,2"),
            ("pos", "nan,0,0"),
            ("pos", "1e39,0,0"),
            ("hp", "abc"),
            ("skill.9", "1"),
            ("nosuchfield", "1"),
        ]
        for name, value in cases:
            with self.subTest(field=name, value=value), self.assertRaises(st.SaveToolError):
                st.set_field(save, name, value)

    def test_skill_bounds_are_checked_within_the_row(self):
        save = st.parse(sample_save())
        st.set_field(save, "skill.1.max", "5")
        st.set_field(save, "skill.1", "5")  # equal to the new max is allowed
        with self.assertRaises(st.SaveToolError):
            st.set_field(save, "skill.1", "6")

    def test_entry_id_pin_refuses_other_layouts_unless_forced(self):
        save = st.parse(sample_save(rynn_id=53))
        with self.assertRaises(st.SaveToolError):
            st.get_field(save, "hp")
        self.assertEqual(st.get_field(save, "pos"), (100.5, -20.25, 3000.0))  # class-only field
        self.assertEqual(st.get_field(save, "hp", force=True), 84)

    def test_ambiguous_or_missing_record_is_refused(self):
        save = st.parse(sample_save())
        rynn_payload = next(
            p for t, _, _, p in sample_entries() if t == TYPE_REC and p[8:12] == struct.pack("<I", RYNN)
        )
        index = len(save.entries)
        save.entries.append(st.Entry(index, TYPE_REC, 56, 0, index, b"", b"", rynn_payload))
        with self.assertRaises(st.SaveToolError):
            st.get_field(save, "hp")  # two id-56 Rynn records
        missing = st.parse(make_srsc([(st.TYPE_SKILLS, st.SKILL_TABLE_ID, 0, skill_table())]))
        with self.assertRaises(st.SaveToolError):
            st.get_field(missing, "hp")

    def test_malformed_files_are_refused(self):
        good = sample_save()
        cases = {
            "bad magic": b"XXXX" + good[4:],
            "truncated": good[:-1],
            "short": b"SRSC",
            "not zlib": make_srsc([(TYPE_REC, 1, 0, b"\x01" * 40)], compress=False),
        }
        for label, blob in cases.items():
            with self.subTest(case=label), self.assertRaises(st.SaveToolError):
                st.parse(blob)

    def test_dump_refuses_non_empty_directory(self):
        src = self.write("d.rsg", sample_save())
        target = self.dir / "dump"
        target.mkdir()
        (target / "keep.txt").write_text("x", encoding="utf-8")
        self.assertEqual(run(["dump", str(src), str(target)])[0], 1)

    def test_pack_refuses_directory_without_manifest(self):
        self.assertEqual(run(["pack", str(self.dir), "-o", str(self.dir / "x.rsg")])[0], 1)

    def test_pack_detects_tampered_raw_file(self):
        src = self.write("t.rsg", sample_save(level=1))
        out_dir = self.dir / "dump"
        run(["dump", str(src), str(out_dir)])
        (out_dir / "e0001.z").write_bytes(b"\x78\x9c")
        self.assertEqual(run(["pack", str(out_dir), "-o", str(self.dir / "x.rsg")])[0], 1)

    def test_fields_table_has_confidence_notes(self):
        for name, field in st.FIELDS.items():
            with self.subTest(field=name):
                self.assertIn(field.confidence, {"high", "medium", "low"})
                self.assertTrue(field.note)
        code, text, _ = run(["fields"])
        self.assertEqual(code, 0)
        self.assertIn("skill.N.max", text)
        self.assertIn("[medium]", text)

    def test_pos_round_trip_float32(self):
        save = st.parse(sample_save())
        st.set_field(save, "pos", "424575.125,160942.844,502777")
        value = st.get_field(st.parse(st.assemble(save)[0]), "pos")
        expected = (424575.125, 160942.844, 502777.0)
        self.assertTrue(all(math.isclose(a, b, rel_tol=1e-6) for a, b in zip(value, expected, strict=True)))


if __name__ == "__main__":
    unittest.main()
