"""Capture reporter-informed shadow evidence from dist, without a save or visual pass/fail claims.

Start with --dry-run; default is one observed water/reflections-on, shadows-off case on DX11.
Use --case repeatedly or --all-cases, --backend d3d11/webgpu/both, and --scene surdana/shadowmire.
Every run owns a new directory under build, fresh cards and all three settings files.
"""

from __future__ import annotations

import argparse
import configparser
import csv
import hashlib
import json
import os
import platform
import re
import struct
import subprocess
from collections import Counter
from datetime import UTC, datetime
from pathlib import Path

from play_run import resolve_level

ROOT = Path(__file__).resolve().parents[2]
PROFILE = Path(__file__).with_name("shadow_review_profile.json")
# All cases retain the reporter log's modern water and reflections. Native=0 is isolation, not a fix.
CASES = {
    "observed-baseline": ("1", False, ""),
    "native-off": ("0", False, ""),
    "both": ("1", True, ""),
    "ray-only": ("0", True, ""),
    "ao": ("0", True, "ao"),
    "direct": ("0", True, "direct"),
    "receiver": ("0", True, "receiver"),
}
SHOTS = ("start", "ground", "yaw1", "yaw2", "skyline1", "skyline2", "skyline3")


def recipe() -> str:
    return "\n".join(
        [
            "clock milliseconds",
            "0 sync play 90",
            "2000 shot start",
            "3000 mouse 0 500",
            "4200 shot ground",
            "5000 mouse 600 0",
            "6200 shot yaw1",
            "7000 mouse 600 0",
            "8200 shot yaw2",
            "9000 mouse 0 -500",
            "10200 shot skyline1",
            "11000 mouse 600 0",
            "12200 shot skyline2",
            "13000 mouse 600 0",
            "14200 shot skyline3",
            "30000 quit",
            "",
        ]
    )


def plan(
    name: str,
    cases: list[str],
    backends: list[str],
    scenes: list[str],
    language: str,
    windowed: bool,
    dump_epoch: int = 1000,
) -> dict:
    if not re.fullmatch(r"[A-Za-z0-9][A-Za-z0-9_.-]*", name) or ".." in name:
        raise ValueError("name must be a single safe directory name")
    base = (ROOT / "build" / "harness" / name).resolve()
    if not base.is_relative_to((ROOT / "build").resolve()):
        raise ValueError("output must resolve inside build")
    if not 1 <= dump_epoch <= 10_000_000:
        raise ValueError("dump epoch must be 1..10000000")
    entries = []
    for scene in scenes:
        level = resolve_level(scene)
        slug = re.sub(r"[^a-z0-9]+", "-", level.lower()).strip("-")
        for backend in backends:
            for case in cases:
                native, shadows, diagnostic = CASES[case]
                folder = base / f"{slug}-{backend}-{case}"
                env = {
                    "PS2X_GS": backend,
                    "PS2X_GS_SCALE": "4",
                    "DRAKAN_WS_ASPECT": "16:9",
                    "DRAKAN_FULLSCREEN": "0" if windowed else "1",
                    "DRAKAN_VSYNC": "0",
                    "DRAKAN_FILTER": "game",
                    "DRAKAN_DEINTERLACE": "weave",
                    "DRAKAN_FLICKER_FILTER": "0",
                    "DRAKAN_WORLD_RATE": "uncapped",
                    "DRAKAN_FOLIAGE_WIND": "1",
                    "DRAKAN_FOLIAGE_STRENGTH": "0.90",
                    "DRAKAN_LANG": language,
                    "DRAKAN_GAME": "d2",
                    "DRAKAN_TITLE_MENU": "modern",
                    "GSR_MODERN_WATER": "1",
                    "GSR_RAY_REFLECTIONS": "1",
                    "GSR_RAY_SHADOWS": str(int(shadows)),
                    "DRAKAN_NATIVE_CHARACTER_SHADOWS": native,
                    "GSR_RAY_DIAGNOSTIC": diagnostic,
                    "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
                    "DRAKAN_START_LEVEL": level,
                    "DRAKAN_INPUT_SCRIPT": str(folder / "recipe.txt"),
                    "DRAKAN_SHOT_DIR": str(folder),
                    "DRAKAN_LOG_DIR": str(folder),
                    "DRAKAN_MEMCARD_DIR": str(folder / "card" / "mc0"),
                    "DRAKAN_INI": str(folder / "drakan.ini"),
                    "DRAKAN_INPUT_INI": str(folder / "drakan_input.ini"),
                    "DRAKAN_NATIVE_OPTIONS_INI": str(folder / "drakan_native_options.ini"),
                    "DRAKAN_MODS": str(folder / "mods"),
                    "DRAKAN_OVERRIDES": str(folder / "overrides"),
                    "GSR_RAY_SCENE_DUMP": str(folder / "ray-scene.bin"),
                    "GSR_TEMPORAL_DUMP_DIR": str(folder),
                    "GSR_TEMPORAL_DUMP_EPOCH": str(dump_epoch),
                    "GSR_RAY_SCENE_DUMP_EPOCH": str(dump_epoch),
                    "GSR_WATER_LOG": "1",
                    "DRAKAN_LIFECYCLE_LOG": "1",
                    "DRAKAN_NO_ACTIVATE": "1",
                    "DRAKAN_PAUSE_UNFOCUSED": "0",
                    "DRAKAN_AUTOPRESS": "3000",
                    "DRAKAN_INPUT_AUTOSTART": "1",
                    "DRAKAN_AUDIO": "0",
                    "DRAKAN_HD_MOVIES": "0",
                    "DRAKAN_DLSS_NEURAL": "0",
                    "DRAKAN_DLSS": "off",
                    "GSR_UPSCALE": "off",
                    "GSR_TEXREPLACE": "0",
                    "GSR_TEXUP": "1",
                    "DRAKAN_SHOW_FPS": "0",
                    "DRAKAN_GHOST": "1",  # projector diagnostic hook; no network session is started
                }
                entries.append(
                    {
                        "case": case,
                        "backend": backend,
                        "level": level,
                        "folder": str(folder),
                        "env": env,
                        "isolated_ini_video": {"window_width": 1280, "window_height": 960, "fullscreen": not windowed},
                        "asset_locations": "Only ISO/ELF paths are read from the deployed INI; all other settings start fresh.",
                    }
                )
    return {
        "schema": 1,
        "profile": json.loads(PROFILE.read_text()),
        "output": str(base),
        "runs": entries,
        "differences": [
            "Current locally built executable and local hardware; reporter CPU/GPU/AE-7 are not emulated.",
            "Fresh isolated New Game cards; direct selected-level boot and camera sweep, location unknown.",
            "Empty per-run mods/developer-overrides directories: base-game isolation; no packs reported, exact installed content unknown.",
            "Audio/movies/DLSS/neural/HD replacement textures and texture upscaling disabled for bounded isolation; reporter settings unknown.",
            "Mouse defaults fixed to direct look, 0.002 radians/count; reporter input settings unknown.",
            f"Requested language {language}; reporter requested German but loaded English. --language english selects observed-resource baseline.",
            "Windowed override" if windowed else "Borderless requested; actual size/refresh depend on local display.",
            "Case overrides explicitly select native/ray shadows; all keep water/reflections enabled.",
            "Existing triangle/metadata/temporal dumps are DXR-only; WebGPU capture cannot establish equivalent caster provenance.",
        ],
        "visual_review": {"status": "pending", "circle_reproduced": None, "fixed": None},
    }


def scene_evidence(folder: Path) -> dict:
    """Verify dump layout/camera agreement, not blocker material acceptance or exact depth epoch identity."""
    result = {"scene": False, "metadata_camera_xz_matches": False, "prepared_depth_matches_size": False}
    scene = folder / "ray-scene.bin"
    metadata = folder / "ray-scene.bin.models.csv"
    if not scene.is_file():
        return result
    with scene.open("rb") as file:
        header = file.read(8)
        if len(header) != 8:
            return result
        magic, size = struct.unpack("<II", header)
        params = file.read(size) if 400 <= size <= 4096 else b""
    if magic != 0x52545831 or len(params) < 400:
        return result
    result.update(scene=True, shader_effects=list(struct.unpack_from("<4I", params, 384)))
    floats = struct.unpack_from("<100f", params)
    result["reflection_constant"] = floats[47] > 0
    if metadata.is_file():
        with metadata.open(newline="") as file:
            rows = list(csv.DictReader(file))
        result["metadata_rows"] = len(rows)
        result["metadata_level_ids"] = sorted({r["level"].lower() for r in rows if r.get("level")})
        result["metadata_camera_xz_matches"] = bool(rows) and all(
            abs(float(r["cameraX"]) - floats[54]) <= 0.1 and abs(float(r["cameraZ"]) - floats[55]) <= 0.1 for r in rows
        )
    for path in folder.glob("*-prepared-depth.raw"):
        with path.open("rb") as file:
            header = file.read(16)
        if len(header) == 16:
            width, height, fmt, stride = struct.unpack("<4I", header)
            result["prepared_depth_matches_size"] |= (
                fmt == 41
                and stride == 4
                and width == floats[44]
                and height == floats[45]
                and path.stat().st_size == 16 + width * height * 4
            )
    result["pairing_limit"] = (
        "Camera X/Z only (frame constants omit Y); raw files contain no independent epoch or target-level tag."
    )
    return result


def classify(text: str, entry: dict, exit_code: int | None, timed_out: bool, files: set[str], dumps: dict) -> dict:
    native, shadows, diagnostic = CASES[entry["case"]]
    load = re.search(r"afterLevelLoad level='" + re.escape(entry["level"]) + r"'[^\n]*\bok=1\b", text)
    loaded = bool(load)
    target_text = text[load.end() :] if load else ""
    # Our first shot follows `sync play 90`; an acknowledged shot after the expected load proves that gate passed.
    control = "screenshot start -> ok" in target_text
    clean = exit_code == 0 and not timed_out and "script quit" in text and "[gsr] exit:" in text
    backend = "[gsr-webgpu] renderer on" in text if entry["backend"] == "webgpu" else "[gsr] D3D11 renderer on" in text
    active = bool(re.search(r"shadows=[1-4] native lights", target_text))
    successful_ray_pass = "DXR scene evaluated:" in target_text or "Ray scene traced:" in target_text
    water = bool(re.search(r"water=1 reflections=1", target_text)) and successful_ray_pass
    webgpu = entry["backend"] == "webgpu"
    native_water = bool(re.search(r"/ [1-9][0-9]* native water", target_text)) and successful_ray_pass
    # WGSL's successful output log has native water/reflection paths but no boolean reflections receipt.
    projector = "native character projection disabled" in target_text
    requested_epoch = int(entry["env"].get("GSR_RAY_SCENE_DUMP_EPOCH", "200"))
    target_dump_epoch = bool(re.search(r"\[modern-water\] epoch=" + str(requested_epoch) + r"\b", target_text))
    target_model_levels = {m.lower() for m in re.findall(r"\[resident-models\] level=([0-9a-fA-F]+)\b", target_text)}
    dump_levels = set(dumps.get("metadata_level_ids", []))
    target_model_level_matches = bool(dump_levels) and "0" not in dump_levels and dump_levels <= target_model_levels
    effects = dumps.get("shader_effects", [])
    mode_bit = {"ao": 64, "direct": 32, "receiver": 128}.get(diagnostic, 0)
    applied = (
        re.search(
            rf"\[ray-diagnostic\] backend={re.escape(entry['backend'])} mode={diagnostic} mask={mode_bit} applied in submitted pass",
            target_text,
        )
        if diagnostic
        else None
    )
    diagnostic_proven = not diagnostic or bool((effects and effects[3] & mode_bit and target_dump_epoch) or applied)
    shots = all(f"{name}.png" in files for name in SHOTS)
    config_lines = [
        line
        for line in text.splitlines()
        if any(
            token in line
            for token in (
                "renderer on",
                "borderless fullscreen",
                "[frame-rate]",
                "[foliage]",
                "language root",
                "[shadow-diagnostic]",
                "[shadow-policy]",
                "[ray-diagnostic]",
            )
        )
    ]
    renderer_config = bool(
        re.search(
            r"window 1280x960, internal scale 4 \(2048x2048\), aspect 1\.778, vsync 0, filter 0, deinterlace 0, flicker_filter 0",
            text,
        )
    )
    partial_renderer_config = webgpu and bool(re.search(r"\[gsr-webgpu\] renderer on .*internal scale 4", text))
    language = entry["env"]["DRAKAN_LANG"].upper()
    language_observed = "language root \\" + language in text
    resource_languages = Counter(
        name.lower()
        for name in re.findall(r"\[hle\] file open 'cdrom0:[/\\]+([A-Za-z]+)[/\\][^\n]* -> (?!not found)", text)
    )
    language_resources = resource_languages[language.lower()] > 0
    cadence_observed = "[frame-rate] uncapped world cadence" in text
    foliage_observed = "[foliage] installed (on, strength 0.90)" in text
    borderless_requested = entry["env"]["DRAKAN_FULLSCREEN"] == "1"
    borderless_observed = "[gsr] borderless fullscreen" in text
    mods = re.findall(r"\[mods\] (\d+) pack\(s\) active", text)
    overrides = re.findall(r"overrides '[^\n]*': (\d+) file\(s\), (\d+) text line\(s\)", text)
    empty_mods = bool(mods) and all(int(n) == 0 for n in mods)
    empty_overrides = bool(overrides) and all(int(files) == 0 and int(lines) == 0 for files, lines in overrides)
    unverified = []
    if webgpu:
        if not renderer_config:
            unverified.append("Full presentation/aspect/filter configuration receipt absent")
        unverified.append("WebGPU log omits reflection enable receipt")
        unverified.append("WebGPU has no equivalent scene/model/depth provenance dump")
    if native == "1":
        unverified.append(
            "Native projector enable is requested; current binary only logs disable, not each native draw"
        )
    missing = []
    for ok, requirement in [
        (loaded, "expected level load"),
        (control, "gameplay control"),
        (clean, "clean scripted shutdown"),
        (backend, "requested backend initialization"),
        (empty_mods, "zero active mod packs"),
        (empty_overrides, "zero developer override files/text lines"),
        ("disc profile pal:" in text, "PAL disc profile"),
        (language_observed, "requested language root"),
        (language_resources, "successful requested-language resource opens"),
        (cadence_observed, "uncapped world cadence"),
        (foliage_observed, "enabled foliage strength 0.90"),
        (borderless_observed if borderless_requested else True, "requested borderless presentation"),
        (shots, "complete camera sweep screenshots"),
        (
            renderer_config or partial_renderer_config,
            "reporter renderer configuration receipt (backend-supported fields)",
        ),
        (active if shadows else "shadows=off" in target_text, "effective ray-shadow state"),
        (native_water if webgpu else water, "successful water pass receipt"),
        (projector if native == "0" else True, "native projector disable receipt"),
        (diagnostic_proven, "diagnostic mask receipt"),
    ]:
        if not ok:
            missing.append(requirement)
    if not webgpu and not (
        dumps.get("scene")
        and dumps.get("metadata_camera_xz_matches")
        and dumps.get("prepared_depth_matches_size")
        and target_dump_epoch
        and target_model_level_matches
    ):
        missing.append("target-scene DXR dump, model camera X/Z and prepared depth pairing")
    return {
        "execution_success": clean,
        "evidence_complete": not missing,
        "evidence_scope": "Backend-supported execution/configuration receipts; visual review and unverified fields remain separate",
        "unverified_configuration": unverified,
        "missing_evidence": missing,
        "expected_level_loaded": loaded,
        "control_observed": control,
        "backend_observed": backend,
        "control_basis": "post-sync-play acknowledged start shot after expected level load",
        "reporter_renderer_config_observed": renderer_config,
        "language_root_observed": language_observed,
        "resource_language_opens": dict(resource_languages),
        "requested_language_resources_observed": language_resources,
        "uncapped_cadence_observed": cadence_observed,
        "reporter_foliage_observed": foliage_observed,
        "borderless_observed": borderless_observed,
        "mod_pack_counts_observed": [int(n) for n in mods],
        "override_counts_observed": [{"files": int(f), "text_lines": int(t)} for f, t in overrides],
        "ray_shadows_active": active,
        "water_reflections_receipt": water,
        "native_water_in_successful_pass": native_water,
        "native_projection": "disabled_observed" if projector else "enabled_requested_unverified",
        "diagnostic": "applied_mask_receipt"
        if applied
        else "mask_observed_in_scene"
        if diagnostic and diagnostic_proven
        else "requested_unverified"
        if diagnostic
        else "normal",
        "effective_config_lines": config_lines,
        "dumps": dumps,
        "dump_target_epoch_observed": target_dump_epoch,
        "dump_epoch_requested": requested_epoch,
        "dump_model_level_matches_target": target_model_level_matches,
        "dump_target_provenance": "Requested epoch logged after target load and CSV level matches target resident-model identity"
        if target_dump_epoch and target_model_level_matches
        else "unproven: epoch receipt or matching target model-level identity absent",
        "visual_review": {"status": "pending", "circle_reproduced": None, "duplicate_reproduced": None, "fixed": None},
    }


def write_settings(folder: Path, exe: Path, fullscreen: bool) -> None:
    source = configparser.ConfigParser(interpolation=None, strict=False)
    source.read(exe.parent / "drakan.ini", encoding="utf-8-sig")
    settings = configparser.ConfigParser()
    locations = {}
    for key in ("iso", "elf"):
        value = source.get("general", key, fallback="")
        locations[key] = str((exe.parent / value).resolve()) if value else ""
    settings["general"] = locations
    settings["video"] = {"window_width": "1280", "window_height": "960", "fullscreen": str(int(fullscreen))}
    with (folder / "drakan.ini").open("w", newline="\n") as file:
        settings.write(file)
    (folder / "drakan_input.ini").write_text(
        "[general]\nlook_mode = direct\n[mouse]\nsensitivity_x = 0.002\nsensitivity_y = 0.002\n", newline="\n"
    )
    (folder / "drakan_native_options.ini").write_text("[options]\nsensitivity = 1\ninvert_y = 0\n", newline="\n")


def executable_hash(exe: Path) -> str:
    with exe.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def screenshot_sizes(folder: Path) -> dict:
    sizes = {}
    for name in SHOTS:
        path = folder / f"{name}.png"
        if path.is_file():
            with path.open("rb") as file:
                header = file.read(24)
            if len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR":
                sizes[path.name] = list(struct.unpack_from(">2I", header, 16))
    return sizes


def run_entry(entry: dict, exe: Path, timeout: int, expected_hash: str) -> dict:
    folder = Path(entry["folder"])
    folder.mkdir(parents=True, exist_ok=False)
    (folder / "card" / "mc0").mkdir(parents=True)
    (folder / "mods").mkdir()
    (folder / "overrides").mkdir()
    (folder / "recipe.txt").write_text(recipe(), newline="\n")
    write_settings(folder, exe, entry["env"]["DRAKAN_FULLSCREEN"] == "1")
    env = {k: v for k, v in os.environ.items() if not k.startswith(("DRAKAN_", "GSR_", "PS2X_"))}
    env.update(entry["env"])
    env.update(DRAKAN_AUTOPRESS_UNTIL=str(timeout * 1000), PS2X_MAX_SECONDS=str(timeout))
    (folder / "request.json").write_text(json.dumps(entry, indent=2) + "\n")
    timed_out = False
    before_hash = executable_hash(exe)
    with (folder / "stdout.log").open("wb") as log:
        with subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT) as process:
            try:
                exit_code = process.wait(timeout=timeout + 15)
            except subprocess.TimeoutExpired:
                timed_out = True
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
                exit_code = process.returncode
    text = (folder / "stdout.log").read_text(errors="replace")
    report = classify(text, entry, exit_code, timed_out, {p.name for p in folder.iterdir()}, scene_evidence(folder))
    after_hash = executable_hash(exe)
    identity_matches = before_hash == after_hash == expected_hash
    report.update(
        case=entry["case"],
        backend=entry["backend"],
        level=entry["level"],
        folder=str(folder),
        exit_code=exit_code,
        timed_out=timed_out,
        executable_sha256_before=before_hash,
        executable_sha256_after=after_hash,
        binary_matches_manifest=identity_matches,
        screenshot_dimensions=screenshot_sizes(folder),
    )
    if not identity_matches:
        report["evidence_complete"] = False
        report["missing_evidence"].append("executable identity changed from manifest or during run")
    (folder / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="shadow-review-" + datetime.now(UTC).strftime("%Y%m%d-%H%M%S"))
    parser.add_argument("--case", action="append", choices=CASES)
    parser.add_argument("--all-cases", action="store_true")
    parser.add_argument("--backend", choices=("d3d11", "webgpu", "both"), default="d3d11")
    parser.add_argument("--scene", action="append", help="New Game level name/code, default surdana; no save needed")
    parser.add_argument("--language", choices=("german", "english"), default="german")
    parser.add_argument("--windowed", action="store_true", help="documented departure from reporter borderless mode")
    parser.add_argument("--timeout", type=int, default=150)
    parser.add_argument(
        "--dump-epoch", type=int, default=1000, help="paired ray/temporal epoch; target provenance is checked"
    )
    parser.add_argument("--dry-run", action="store_true")
    args = parser.parse_args()
    if not 30 <= args.timeout <= 900:
        parser.error("--timeout must be 30..900 seconds")
    cases = list(CASES) if args.all_cases else list(dict.fromkeys(args.case or ["observed-baseline"]))
    backends = ["d3d11", "webgpu"] if args.backend == "both" else [args.backend]
    try:
        manifest = plan(
            args.name,
            cases,
            backends,
            list(dict.fromkeys(args.scene or ["surdana"])),
            args.language,
            args.windowed,
            args.dump_epoch,
        )
    except ValueError as error:
        parser.error(str(error))
    exe = (ROOT / "dist" / "DrakanPC" / "drakan.exe").resolve()
    manifest.update(
        executable=str(exe), host={"os": platform.platform(), "cpu": platform.processor()}, timeout=args.timeout
    )
    for entry in manifest["runs"]:
        entry["env"].update(DRAKAN_AUTOPRESS_UNTIL=str(args.timeout * 1000), PS2X_MAX_SECONDS=str(args.timeout))
    if args.dry_run:
        print(json.dumps(manifest, indent=2))
        return 0
    if not exe.is_file():
        parser.error("deployed dist/DrakanPC/drakan.exe missing; use Build-LocalGame.ps1")
    manifest["executable_sha256"] = executable_hash(exe)
    out = Path(manifest["output"])
    out.mkdir(parents=True, exist_ok=False)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    results = [run_entry(entry, exe, args.timeout, manifest["executable_sha256"]) for entry in manifest["runs"]]
    (out / "summary.json").write_text(
        json.dumps(
            {
                "runs": results,
                "same_executable_as_manifest": all(r["binary_matches_manifest"] for r in results),
                "visual_review": "pending; no fix verdict",
            },
            indent=2,
        )
        + "\n"
    )
    print(out)
    return 0 if all(result["evidence_complete"] for result in results) else 1


if __name__ == "__main__":
    raise SystemExit(main())
