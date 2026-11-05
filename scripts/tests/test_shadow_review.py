import json
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
from shadow_review import CASES, SHOTS, classify, plan, scene_evidence, write_settings


class ShadowReviewTest(unittest.TestCase):
    def entry(self, case="observed-baseline", backend="d3d11"):
        return plan("review-test", [case], [backend], ["surdana"], "german", False)["runs"][0]

    def text(self):
        return (
            "[gsr] D3D11 renderer on 'local GPU', window 1280x960, internal scale 4 (2048x2048), "
            "aspect 1.778, vsync 0, filter 0, deinterlace 0, flicker_filter 0\n"
            "[drakan] disc profile pal: features=0\n[drakan] language root \\GERMAN\n"
            "[hle] file open 'cdrom0:/German/Drakan 2.rrc' -> GERMAN\\DRAKAN2.RRC handle 1\n"
            "[frame-rate] uncapped world cadence\n[foliage] installed (on, strength 0.90)\n"
            "[gsr] borderless fullscreen\n"
            "[mods] 0 pack(s) active, 0 entries, 0 text lines\n"
            "[drakan] overrides 'private': 0 file(s), 0 text line(s)\n"
            "afterLevelLoad level='Level 1A - Surdana' ok=1\n"
            "[in] screenshot start -> ok\nwater=1 reflections=1 shadows=0\n"
            "DXR scene evaluated: shadows=off\n[in] script quit\n[gsr] exit: frames=100\n"
        )

    def test_baseline_preserves_observed_water_without_claiming_shadow_toggle(self):
        p = plan("review-test", list(CASES), ["d3d11", "webgpu"], ["surdana"], "german", False)
        self.assertEqual(len(p["runs"]), len(CASES) * 2)
        env = p["runs"][0]["env"]
        self.assertEqual((env["GSR_MODERN_WATER"], env["GSR_RAY_REFLECTIONS"], env["GSR_RAY_SHADOWS"]), ("1", "1", "0"))
        self.assertEqual(env["PS2X_GS_SCALE"], "4")
        self.assertEqual(env["DRAKAN_LANG"], "german")
        self.assertEqual(env["DRAKAN_GHOST"], "1")
        self.assertEqual(env["GSR_RAY_SCENE_DUMP_EPOCH"], "1000")
        self.assertEqual(env["GSR_TEMPORAL_DUMP_EPOCH"], "1000")
        self.assertNotIn("DRAKAN_LANGUAGE", env)
        self.assertNotIn("DRAKAN_WINDOW_WIDTH", env)
        self.assertTrue(Path(env["DRAKAN_MEMCARD_DIR"]).is_absolute())
        self.assertEqual(Path(env["DRAKAN_MODS"]).parent, Path(env["DRAKAN_MEMCARD_DIR"]).parents[1])
        self.assertEqual(Path(env["DRAKAN_OVERRIDES"]).parent, Path(env["DRAKAN_MODS"]).parent)
        self.assertNotIn("auto", CASES)
        self.assertIn("English", p["profile"]["reporter_known"]["language_resources_observed"])
        with self.assertRaises(ValueError):
            plan("../escape", [], [], [], "german", False)

    def test_execution_and_visual_review_are_separate(self):
        files = {f"{name}.png" for name in SHOTS}
        text = self.text() + "[resident-models] level=abc instances=2\n[modern-water] epoch=1000 DXR scene evaluated:\n"
        result = classify(
            text,
            self.entry(),
            0,
            False,
            files,
            {
                "scene": True,
                "metadata_camera_xz_matches": True,
                "prepared_depth_matches_size": True,
                "metadata_level_ids": ["abc"],
            },
        )
        self.assertTrue(result["execution_success"])
        self.assertTrue(result["evidence_complete"])
        self.assertIsNone(result["visual_review"]["fixed"])
        self.assertFalse(classify(self.text(), self.entry(), 0, True, files, {})["execution_success"])
        self.assertFalse(classify("[gsr] exit:", self.entry(), 0, False, files, {})["evidence_complete"])

    def test_cliff_requires_settled_position_before_face_shot(self):
        entry = plan("test", ["both"], ["d3d11"], ["surdana-cliff"], "german", True)["runs"][0]
        xyz = " ".join(entry["env"]["DRAKAN_PLAYTEST_POS"].split()[:3])
        position = f"[playtest] +240 boundaries: player at {xyz} yaw -0.785\n"
        shot = "[in] screenshot cliff_face -> ok\n"
        reason = "settled player at cliff before first screenshot"
        for logs, expected in ((position + shot, False), (shot + position, True), (position, True)):
            result = classify(self.text() + logs, entry, 0, False, set(), {})
            self.assertEqual(reason in result["missing_evidence"], expected)
            self.assertIn("three cliff-face angles", result["missing_evidence"])

    def test_cliff_legacy_sky_requires_runtime_and_scene_evidence(self):
        run = plan("test", ["legacy-sky", "both"], ["d3d11"], ["surdana-cliff"], "german", True)["runs"]
        self.assertEqual(run[0]["env"]["DRAKAN_RAY_INCLUDE_SKY"], "1")
        self.assertNotIn("DRAKAN_RAY_INCLUDE_SKY", run[1]["env"])
        for entry, count, mismatch in ((run[0], 0, True), (run[0], 1, False), (run[1], 1, True)):
            result = classify(self.text(), entry, 0, False, set(), {"surdana_planet_instances": count})
            self.assertEqual("expected Surdana planet model membership" in result["missing_evidence"], mismatch)
            self.assertEqual(
                "legacy sky inclusion receipt" in result["missing_evidence"], entry["case"] == "legacy-sky"
            )

    def test_requested_flags_do_not_prove_active_shadows_or_diagnostic(self):
        files = {f"{name}.png" for name in SHOTS}
        result = classify(self.text(), self.entry("ao"), 0, False, files, {})
        self.assertFalse(result["ray_shadows_active"])
        self.assertEqual(result["diagnostic"], "requested_unverified")
        self.assertFalse(result["evidence_complete"])
        self.assertIn("native projector disable receipt", result["missing_evidence"])

    def test_control_marker_before_expected_load_does_not_count(self):
        text = "[in] screenshot start -> ok\nafterLevelLoad level='Level 1A - Surdana' ok=1\n"
        result = classify(text, self.entry(), 0, False, set(), {})
        self.assertFalse(result["control_observed"])

    def test_failed_target_load_and_earlier_receipts_do_not_count(self):
        text = (
            "afterLevelLoad level='Level 1A - Surdana' ok=0\n"
            "screenshot start -> ok\nshadows=1 native lights\nwater=1 reflections=1\n"
            "DXR scene evaluated:\nnative character projection disabled\n[modern-water] epoch=200\n"
            "afterLevelLoad level='Level 1A - Surdana' ok=1\n"
        )
        result = classify(text, self.entry("ray-only"), 0, False, set(), {})
        self.assertTrue(result["expected_level_loaded"])
        self.assertFalse(result["control_observed"])
        self.assertFalse(result["ray_shadows_active"])
        self.assertFalse(result["water_reflections_receipt"])
        self.assertFalse(result["dump_target_epoch_observed"])
        self.assertEqual(result["native_projection"], "enabled_requested_unverified")

    def test_webgpu_supported_receipts_can_complete_without_fabricating_reflections(self):
        text = (
            self.text()
            .replace(
                "[gsr] D3D11 renderer on 'local GPU', window 1280x960, internal scale 4 (2048x2048), "
                "aspect 1.778, vsync 0, filter 0, deinterlace 0, flicker_filter 0",
                "[gsr-webgpu] renderer on local GPU, internal scale 4, native blending",
            )
            .replace("water=1 reflections=1 shadows=0", "Ray scene traced: 100 opaque / 20 native water; shadows=off")
        )
        result = classify(text, self.entry(backend="webgpu"), 0, False, {f"{s}.png" for s in SHOTS}, {})
        self.assertTrue(result["evidence_complete"])
        self.assertFalse(result["water_reflections_receipt"])
        self.assertTrue(result["unverified_configuration"])

    def test_applied_backend_mask_receipt_is_scoped_and_exact(self):
        text = self.text().replace("shadows=off", "shadows=1 native lights")
        text += "Ray scene traced: 100 opaque / 20 native water\nnative character projection disabled\n"
        text += "[gsr-webgpu] renderer on local GPU, internal scale 4, native blending\n"
        receipt = "[ray-diagnostic] backend=webgpu mode=direct mask=32 applied in submitted pass\n"
        args = (self.entry("direct", "webgpu"), 0, False, {f"{s}.png" for s in SHOTS}, {})
        self.assertEqual(classify(text + receipt, *args)["diagnostic"], "applied_mask_receipt")
        self.assertTrue(classify(text + receipt, *args)["evidence_complete"])
        self.assertFalse(classify(receipt + text, *args)["evidence_complete"])
        self.assertFalse(classify(text + receipt.replace("mask=32", "mask=64"), *args)["evidence_complete"])

    def test_scene_metadata_requires_matching_camera_and_depth(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            params = bytearray(512)
            struct.pack_into("<2f", params, 44 * 4, 2, 3)
            struct.pack_into("<2f", params, 54 * 4, 100, 200)
            struct.pack_into("<4I", params, 384, 1, 1, 1, 64)
            (folder / "ray-scene.bin").write_bytes(struct.pack("<2I", 0x52545831, 512) + params)
            sidecar = folder / "ray-scene.bin.models.csv"
            sidecar.write_text("cameraX,cameraZ\n100,200\n")
            (folder / "f1-s1-prepared-depth.raw").write_bytes(struct.pack("<4I", 2, 3, 41, 4) + bytes(24))
            result = scene_evidence(folder)
            self.assertTrue(result["metadata_camera_xz_matches"])
            self.assertTrue(result["prepared_depth_matches_size"])
            self.assertEqual(result["shader_effects"][3], 64)
            sidecar.write_text("cameraX,cameraZ\n101,200\n")
            self.assertFalse(scene_evidence(folder)["metadata_camera_xz_matches"])

    def test_settings_copy_only_local_asset_locations(self):
        with tempfile.TemporaryDirectory() as tmp:
            folder = Path(tmp)
            source = folder / "dist"
            source.mkdir()
            (source / "drakan.ini").write_text("[general]\niso = private.iso\nlanguage = french\n[video]\nscale = 8\n")
            out = folder / "out"
            out.mkdir()
            write_settings(out, source / "drakan.exe", False)
            text = (out / "drakan.ini").read_text()
            self.assertIn(str((source / "private.iso").resolve()), text)
            self.assertIn("window_width = 1280", text)
            self.assertNotIn("scale = 8", text)
            self.assertNotIn("french", text)
            json.dumps(self.entry())


if __name__ == "__main__":
    unittest.main()
