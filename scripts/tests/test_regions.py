"""Disc region profiles (docs/reference/regions.md): regions.json, the USA recompiler config and the runtime table.

Public metadata only; no ELF or ISO is read.
"""

import copy
import csv
import importlib.util
import json
import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
spec = importlib.util.spec_from_file_location("prepare_iso", ROOT / "scripts/buildtool/prepare_iso.py")
prepare_iso = importlib.util.module_from_spec(spec)
spec.loader.exec_module(prepare_iso)
spec = importlib.util.spec_from_file_location("runtime_profile", ROOT / "scripts/buildtool/generate_runtime_profile.py")
runtime_profile = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runtime_profile)

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
            for key in ("config", "functions_csv", "jump_tables_csv", "runtime_profile"):
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
        profile = json.loads((ROOT / REGIONS["regions"]["usa"]["runtime_profile"]).read_text())
        pairs = [(int(a, 16), int(b, 16)) for a, b in profile["addresses"].items()]
        self.assertGreater(len(pairs), 5)
        self.assertEqual(pairs, sorted(pairs))
        maps = {}
        for name, key, value in (
            ("function_map.csv", "pal_start_hex", "region_start_hex"),
            ("data_map.csv", "pal_addr_hex", "region_addr_hex"),
        ):
            with (ROOT / "analysis/regions/usa" / name).open(newline="") as f:
                for row in csv.DictReader(f):
                    if "conflicting_refs" in row and int(row[key], 16) in dict(pairs):
                        self.assertEqual(int(row["conflicting_refs"]), 0)
                    maps.setdefault(int(row[key], 16), int(row[value], 16))
        # PadMgr fields audited in both bodies; the maps pair the object base.
        for offset in (0x498, 0x4D4):
            maps[0x4E90C0 + offset] = maps[0x4E90C0] + offset
        # Rows the maps cannot confirm were read from both bodies by hand (regions.md "Runtime address table").
        hand_checked = {0x00516F80, 0x0056DF00}  # sceSdRemote's end-function slot and RPC buffer
        for pal, usa in pairs:
            if pal in hand_checked:
                continue
            self.assertEqual(maps.get(pal), usa, f"{pal:#x}")

    def test_all_runtime_profiles_validate(self):
        for region in REGIONS["regions"]:
            runtime_profile.load_profile(ROOT / "scripts/buildtool/regions.json", ROOT, region)

    def test_missing_mapping_and_layout_fail_closed(self):
        identity, profile = runtime_profile.load_profile(ROOT / "scripts/buildtool/regions.json", ROOT, "usa")
        for address in (
            0x101710,
            0x4FB60C,
            0x1767A0,
            0x440610,
            0x4FB600,
            0x4DB4F0,
            0x4FB6D0,
            0x1E3970,
            0x4424E0,
            0x4FBAFC,
            0x5565D4,
            *runtime_profile.REQUIREMENTS["DirectMouseLook"],
        ):
            incomplete = copy.deepcopy(profile)
            del incomplete["addresses"][f"0x{address:08x}"]
            with self.assertRaisesRegex(ValueError, "Missing verified addresses"):
                runtime_profile.validate("usa", identity, incomplete)
        incomplete = copy.deepcopy(profile)
        del incomplete["audio"]
        with self.assertRaisesRegex(ValueError, "explicit audio layout"):
            runtime_profile.validate("usa", identity, incomplete)
        for field in runtime_profile.MOVIE_FIELDS:
            incomplete = copy.deepcopy(profile)
            del incomplete["movie"][field]
            with self.assertRaisesRegex(ValueError, "explicit movie layout"):
                runtime_profile.validate("usa", identity, incomplete)

    def test_input_and_settings_layouts_fail_closed(self):
        identity, profile = runtime_profile.load_profile(ROOT / "scripts/buildtool/regions.json", ROOT, "usa")
        for layout in ("input_flow", "settings", "camera"):
            for field in profile[layout]:
                invalid = copy.deepcopy(profile)
                del invalid[layout][field]
                with self.assertRaisesRegex(ValueError, "explicit"):
                    runtime_profile.validate("usa", identity, invalid)
        for layout, key, value in [
            ("input_flow", "pause_screen", 32),
            ("input_flow", "context_camera", 3),
            ("input_flow", "menu_screens", 0),
            ("input_flow", "gameplay_modes", 0),
            ("settings", "widget_id", 65536),
            ("camera", "camera_pitch", 3),
            ("camera", "player_target", 65536),
            ("camera", "resource_stride", 0),
        ]:
            invalid = copy.deepcopy(profile)
            invalid[layout][key] = value
            with self.assertRaises(ValueError):
                runtime_profile.validate("usa", identity, invalid)
        invalid = copy.deepcopy(profile)
        invalid["features"].remove("InputFlow")
        with self.assertRaisesRegex(ValueError, "require InputFlow"):
            runtime_profile.validate("usa", identity, invalid)

    def test_invalid_features_addresses_and_timing_rejected(self):
        identity, profile = runtime_profile.load_profile(ROOT / "scripts/buildtool/regions.json", ROOT, "usa")
        mutations = [
            lambda p: p["features"].append("Widescreen"),
            lambda p: p["features"].append("Typo"),
            lambda p: p["features"].append("MovieGuard"),
            lambda p: p.update(identity_addresses=True),
            lambda p: p["addresses"].update({"0x00101710": "0x0"}),
            lambda p: p["addresses"].update({"0x101710": "0x1016b0"}),
            lambda p: p["movie"].update(fps_numerator=0),
            lambda p: p["movie"].update(fps_denominator=0),
            lambda p: p["movie"].update(screen_count=257),
            lambda p: p["movie"].update(frame_count="0x2d"),
            lambda p: p["audio"].update(manager_stereo=0),
            lambda p: p["addresses"].update({"0x004db4f0": "0x01fffffc"}),
        ]
        for mutation in mutations:
            invalid = copy.deepcopy(profile)
            mutation(invalid)
            with self.assertRaises(ValueError):
                runtime_profile.validate("usa", identity, invalid)

    def test_movement_and_combat_observer_fail_closed(self):
        identity, profile = runtime_profile.load_profile(ROOT / "scripts/buildtool/regions.json", ROOT, "usa")
        mutations = [
            lambda p: p["addresses"].pop("0x004e90c0"),
            lambda p: p["addresses"].pop("0x00131fa8"),
            lambda p: p["addresses"].pop("0x004af7f8"),
            lambda p: p["features"].remove("DirectMouseLook"),
            lambda p: p["addresses"].pop("0x004fbac8"),
            lambda p: p["combat_observer"].pop("player_guard_flags"),
            lambda p: p["combat_observer"].update(player_weapon=3),
        ]
        for mutation in mutations:
            invalid = copy.deepcopy(profile)
            mutation(invalid)
            with self.assertRaises(ValueError):
                runtime_profile.validate("usa", identity, invalid)

    def test_new_disc_generates_without_region_specific_code(self):
        registry = copy.deepcopy(REGIONS)
        registry["regions"]["test-disc"] = dict(REGIONS["regions"]["usa"], boot="SCUS_999.99", save_id="BASCUS-99999")
        with tempfile.TemporaryDirectory() as tmp:
            registry_path = Path(tmp) / "regions.json"
            registry_path.write_text(json.dumps(registry))
            output = Path(tmp) / "profile.h"
            command = [
                sys.executable,
                str(ROOT / "scripts/buildtool/generate_runtime_profile.py"),
                "--registry",
                str(registry_path),
                "--root",
                str(ROOT),
                "--region",
                "test-disc",
                "--output",
                str(output),
            ]
            subprocess.run(command, check=True, capture_output=True)
            self.assertIn('"test-disc", "SCUS_999.99"', output.read_text())
            self.assertIn('"BASCUS-99999"', output.read_text())
            timestamp = output.stat().st_mtime_ns
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(output.stat().st_mtime_ns, timestamp)
            command[command.index("test-disc")] = "missing"
            result = subprocess.run(command, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("Unknown disc profile", result.stderr)

    def test_usa_config_is_current_and_new_missing_exports_fail(self):
        with tempfile.TemporaryDirectory() as tmp:
            output = Path(tmp) / "usa.toml"
            command = [
                sys.executable,
                "-I",
                "analysis/scripts/region_config.py",
                "--map",
                "analysis/regions/usa",
                "--pal-functions",
                "analysis/functions.csv",
                "--template",
                "configs/drakan.toml",
                "--boot",
                "SCUS_971.28",
                "--functions-csv",
                "../analysis/regions/usa/functions.csv",
                "--jump-tables-csv",
                "../analysis/regions/usa/jumptables.csv",
                "--out",
                str(output),
            ]
            subprocess.run(command, cwd=ROOT, check=True, capture_output=True)
            expected = (ROOT / "configs/drakan-usa.toml").read_text(encoding="utf-8")
            self.assertEqual(output.read_text(encoding="utf-8"), expected)
            template = Path(tmp) / "future.toml"
            template.write_text(
                (ROOT / "configs/drakan.toml")
                .read_text(encoding="utf-8")
                .replace('"sceSifAllocSysMemory@0x004C2DF8"', '"FutureExport@0x00900000"'),
                encoding="utf-8",
            )
            command[command.index("configs/drakan.toml")] = str(template)
            result = subprocess.run(command, cwd=ROOT, capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("FutureExport@0x00900000", result.stderr)
            self.assertEqual(output.read_text(encoding="utf-8"), expected)


if __name__ == "__main__":
    unittest.main()
