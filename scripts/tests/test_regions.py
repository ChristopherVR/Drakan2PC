"""Disc region profiles (docs/reference/regions.md): regions.json, the USA recompiler config and the runtime table.

Public metadata only; no ELF or ISO is read.
"""

import csv
import importlib.util
import json
import re
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_iso", ROOT / "scripts/buildtool/prepare_iso.py")
prepare_iso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare_iso)

REGIONS = json.loads((ROOT / "scripts/buildtool/regions.json").read_text(encoding="utf-8"))


class RegionProfileTests(unittest.TestCase):
    def test_pal_profile_matches_the_toolchain_lock(self):
        lock = json.loads((ROOT / "scripts/buildtool/toolchain.lock.json").read_text())["supported_game"]
        pal = REGIONS["regions"]["pal"]
        self.assertEqual(REGIONS["default"], "pal")
        self.assertEqual((pal["boot"], pal["elf_sha256"]), (lock["boot"], lock["elf_sha256"]))
        self.assertEqual(pal["config"], "configs/drakan.toml")
        self.assertEqual(pal["functions_csv"], "analysis/functions.csv")

    def test_every_profile_points_at_tracked_metadata(self):
        for region, profile in REGIONS["regions"].items():
            for key in ("config", "functions_csv", "jump_tables_csv"):
                self.assertTrue((ROOT / profile[key]).is_file(), f"{region} {key}")
            self.assertRegex(profile["boot"], r"^S[CL](?:ES|US)_\d{3}\.\d{2}$")
            self.assertEqual(len(profile["vu_chunks"]), 6)

    def test_boot_name_selects_the_region(self):
        self.assertEqual(prepare_iso.region_for_boot("SCES_500.06")[0], "pal")
        self.assertEqual(prepare_iso.region_for_boot("scus_971.28 ")[0], "usa")
        self.assertIsNone(prepare_iso.region_for_boot("SLUS_123.45"))

    def test_default_template_follows_the_region(self):
        usa = REGIONS["regions"]["usa"]
        self.assertEqual(prepare_iso.region_template(ROOT / "configs/drakan.toml", ROOT, usa), ROOT / usa["config"])
        other = ROOT / "configs/custom.toml"
        self.assertEqual(prepare_iso.region_template(other, ROOT, usa), other)

    def test_usa_config_is_a_relative_public_template(self):
        config = tomllib.loads((ROOT / "configs/drakan-usa.toml").read_text(encoding="utf-8"))
        general = config["general"]
        for key in ("input", "output", "functions_csv", "jump_tables_csv"):
            self.assertFalse(Path(general[key]).is_absolute())
            self.assertNotIn(":", general[key])
        self.assertIn("SCUS_971.28", general["input"])
        self.assertIn("regions/usa", general["functions_csv"])
        pal = tomllib.loads((ROOT / "configs/drakan.toml").read_text(encoding="utf-8"))["general"]
        names = lambda entries: {e.split("@")[0] for e in entries}
        # Only the four SDK 2.3 entries missing from the USA SDK 2.2 libraries are dropped.
        self.assertEqual(
            names(pal["stubs"]) - names(general["stubs"]),
            {"sceSifAllocSysMemory"},
        )
        self.assertEqual(
            names(pal["entry_points"]) - names(general["entry_points"]),
            {"sceSifUnloadModule", "sceSifSearchModuleByName", "sceSifSearchModuleByAddress"},
        )

    def test_usa_stubs_start_usa_functions(self):
        config = tomllib.loads((ROOT / "configs/drakan-usa.toml").read_text(encoding="utf-8"))["general"]
        with (ROOT / "analysis/regions/usa/functions.csv").open(newline="") as f:
            starts = {int(r["start_addr_hex"], 16) for r in csv.DictReader(f)}
        for entry in config["stubs"]:
            self.assertIn(int(entry.split("@0x")[1], 16), starts, entry)

    def test_runtime_usa_table_agrees_with_the_maps(self):
        source = (ROOT / "ps2recomp/ps2xRuntime/src/lib/drakan_region.cpp").read_text(encoding="utf-8")
        table = source.split("constexpr AddressPair kUsa[] = {", 1)[1].split("};", 1)[0]
        pairs = [(int(a, 16), int(b, 16)) for a, b in re.findall(r"\{0x([0-9a-f]+)u, 0x([0-9a-f]+)u\}", table)]
        self.assertGreater(len(pairs), 5)
        self.assertEqual(pairs, sorted(pairs))
        maps = {}
        for name, key, value in (
            ("function_map.csv", "pal_start_hex", "region_start_hex"),
            ("data_map.csv", "pal_addr_hex", "region_addr_hex"),
        ):
            with (ROOT / "analysis/regions/usa" / name).open(newline="") as f:
                for row in csv.DictReader(f):
                    maps.setdefault(int(row[key], 16), int(row[value], 16))
        # Rows the maps cannot confirm were read from both bodies by hand (regions.md "Runtime address table").
        hand_checked = {0x00516F80, 0x0056DF00}  # sceSdRemote's end-function slot and RPC buffer
        for pal, usa in pairs:
            if pal in hand_checked:
                continue
            self.assertEqual(maps.get(pal), usa, f"{pal:#x}")


if __name__ == "__main__":
    unittest.main()
