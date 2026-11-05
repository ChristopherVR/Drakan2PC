"""Native PAL spell gesture checks, with isolated cards/settings and actual RAM evidence.

Supply a recipe preparing a known projectile spell through native input, entering the
same valid sequence with WASD and arrows, and firing each. Required dump labels:
ram_{wasd,arrows}_{before,prepared,step1..stepN,recognized,released,projectile}.
Native recognition and cast acceptance are checked separately from projectile visibility.
A completed script alone fails. Discovery unlocks through retail pad input; no direct
guest state/history writes are added.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import re
import shutil
import struct
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

from combat_ram_report import report as combat_report
from dual_wield_check import patch_ini
from play_run import ROOT, run_once
from settings_check import Driver, state


def snapshot(path, profile):
    if not profile["identity_addresses"]:
        raise ValueError("Spell history observer currently audited for PAL only")
    data = path.read_bytes()
    if len(data) != 0x2000000:
        raise ValueError("Expected a complete 32 MiB RAM dump")

    def u32(a):
        a &= 0x1FFFFFFF
        if not 0x100000 <= a <= len(data) - 4:
            raise ValueError("Invalid guest pointer in spell observer")
        return struct.unpack_from("<I", data, a)[0]

    layout = profile["camera"]

    def offset(name):
        value = layout[name]
        return int(value, 0) if isinstance(value, str) else value

    context = u32(0x5565D4)
    handle = u32(context + offset("context_actor"))
    player = u32(u32(handle + offset("handle_inner")) + offset("inner_data"))
    base = player + 0x1024
    vector, capacity, count = (u32(base + x) for x in (0x3C, 0x40, 0x44))
    if count > capacity or capacity > 4096:
        raise ValueError("Invalid native gesture history bounds")
    history = []
    for i in range(count):
        words = [u32(vector + i * 8 + j * 4) for j in range(2)]
        xy = [struct.unpack("<f", struct.pack("<I", word))[0] for word in words]
        if not all(math.isfinite(v) for v in xy):
            raise ValueError("Nonfinite native gesture history")
        history.append(xy)
    processed = bytes((data[(base + 16 + i) & 0x1FFFFFFF]) for i in range(12)).split(b"\0", 1)[0]
    if any(c not in b"UDLR" for c in processed):
        raise ValueError("Invalid native processed gesture string")
    spells = []
    table, spell_count = u32(base + 4), u32(base + 12)
    if spell_count > 32:
        raise ValueError("Invalid native spell table")
    for i in range(spell_count):
        entry = table + i * 20
        pointer, length = u32(entry + 8), u32(entry + 16)
        if length > 16:
            raise ValueError("Invalid native spell gesture length")
        sequence = ""
        for j in range(length):
            xy = [struct.unpack("<f", struct.pack("<I", u32(pointer + j * 8 + k * 4)))[0] for k in range(2)]
            direction = {(-1.0, 0.0): "L", (1.0, 0.0): "R", (0.0, -1.0): "U", (0.0, 1.0): "D"}.get(tuple(xy))
            if direction is None:
                raise ValueError("Invalid native spell gesture direction")
            sequence += direction
        spells.append({"index": i, "level": u32(entry), "sequence": sequence})
    combat = combat_report(path)
    return {
        "actor": handle,
        "summon_state": u32(base + 0x1C),
        "history": history,
        "processed": processed.decode("ascii"),
        "selected_spell": u32(base + 72),
        "mana": combat.get("mana"),
        "spells": spells,
        "projectiles": combat.get("spell_projectiles", []),
        "rynn_available": combat.get("rynn_available", False),
    }


def check_evidence(samples, sequence):
    checks = {}
    processed = {}
    for controls in ("wasd", "arrows"):
        labels = [
            "before",
            "prepared",
            *[f"step{i + 1}" for i in range(len(sequence))],
            "recognized",
            "released",
            "projectile",
        ]
        group = {label: samples.get(f"ram_{controls}_{label}") for label in labels}
        checks[f"{controls}_required_samples"] = all(group.values())
        if not all(group.values()):
            continue
        before, prepared = group["before"], group["prepared"]
        recognized, released = group["recognized"], group["released"]
        checks[f"{controls}_native_preparation"] = before["summon_state"] in (0, 5) and prepared["summon_state"] in (
            1,
            2,
            3,
            4,
        )
        checks[f"{controls}_empty_prepared_history"] = not prepared["processed"]
        checks[f"{controls}_all_samples_available"] = all(s["rynn_available"] for s in group.values())
        checks[f"{controls}_same_actor"] = len({s["actor"] for s in group.values()}) == 1
        expected = {
            spell["index"] for spell in recognized["spells"] if spell["level"] and spell["sequence"] == sequence
        }

        def native_recognized(sample, expected=expected):
            return sample["summon_state"] == 5 and not sample["processed"] and sample["selected_spell"] in expected

        checks[f"{controls}_selection_reset_on_preparation"] = prepared["selected_spell"] == 0xFFFFFFFF
        for i, direction in enumerate(sequence, 1):
            sample = group[f"step{i}"]
            checks[f"{controls}_gesture_{i}_{direction}"] = (
                sample["processed"] == sequence[:i] if i < len(sequence) else native_recognized(sample)
            )
        checks[f"{controls}_recognized_history"] = native_recognized(recognized)
        checks[f"{controls}_native_release"] = released["summon_state"] in (0, 5)
        original = {p["handle"] for p in before["projectiles"]}
        checks[f"{controls}_spell_launched"] = any(
            p["handle"] not in original
            for name, sample in samples.items()
            if name.startswith(f"ram_{controls}_projectile")
            for p in sample["projectiles"]
        )
        checks[f"{controls}_native_cast_started"] = any(
            sample["summon_state"] == 4
            for name, sample in samples.items()
            if name.startswith(f"ram_{controls}_projectile")
        )
        costs = [
            sample["mana"]
            for name, sample in samples.items()
            if name.startswith(f"ram_{controls}_projectile") and sample.get("mana") is not None
        ]
        ready = samples.get(f"ram_{controls}_ready", recognized)
        checks[f"{controls}_native_mana_spent"] = (
            bool(costs) and ready.get("mana") is not None and min(costs) < ready["mana"]
        )
        processed[controls] = recognized["selected_spell"] if native_recognized(recognized) else None
    checks["matching_native_histories"] = set(processed) == {"wasd", "arrows"} and (
        processed["wasd"] is not None and processed["wasd"] == processed["arrows"]
    )
    return checks


def setup_recipe():
    # Retail native pad cheat, already used by combat_encounter_cheatscout.txt.
    lines = ["1 log on 1", "0 sync play 90"]
    buttons = (
        "L1",
        "R2",
        "L2",
        "R1",
        "DPAD_UP",
        "DPAD_DOWN",
        "DPAD_LEFT",
        "DPAD_RIGHT",
        "DPAD_RIGHT",
        "DPAD_LEFT",
        "DPAD_DOWN",
        "DPAD_UP",
    )
    lines += [f"{2 + i * 4} pad tap {button}" for i, button in enumerate(buttons)]
    return lines


def magic_context_recipe():
    # Native Spells inventory selection is fixture preparation only. The first
    # launch empties the selected spell before any gesture evidence baseline.
    return [
        *setup_recipe(),
        "60 dump ram_unlocked",
        "70 key tap TAB",
        "160 key tap ENTER",
        "180 key tap PGDN",
        "260 key tap ENTER",
        "280 key tap E",
        "300 key tap PGUP",
        "420 pad down SELECT",
        "428 pad up SELECT",
        "470 sync play 90",
        "5 dump ram_fixture_selected",
        "20 button down 1",
        "26 button up 1",
        "35 dump ram_fixture_launch",
        "90 sync play 30",
    ]


def discovery_recipe():
    return (
        "\n".join(
            [
                *magic_context_recipe(),
                "5 dump ram_fixture_idle",
                "10 button down 2",
                "12 dump ram_rmb_down",
                "14 button up 2",
                "16 dump ram_rmb_up",
                "35 dump ram_prep35",
                "55 dump ram_prep55",
                "70 key down A",
                "72 dump ram_a_down",
                "76 key up A",
                "100 dump ram_after_a",
                "130 dump ram_discovery",
                "135 shot discovery",
                "145 quit",
            ]
        )
        + "\n"
    )


def gesture_recipe(sequence):
    lines = magic_context_recipe()
    keys = {
        "wasd": dict(zip("UDLR", ("W", "S", "A", "D"))),
        "arrows": dict(zip("UDLR", ("UP", "DOWN", "LEFT", "RIGHT"))),
    }
    t = 90
    for controls in ("wasd", "arrows"):
        lines += [f"{t} dump ram_{controls}_before", f"{t + 2} button down 2", f"{t + 55} dump ram_{controls}_prepared"]
        t += 65
        for i, direction in enumerate(sequence, 1):
            key = keys[controls][direction]
            lines += [f"{t} key down {key}", f"{t + 5} key up {key}", f"{t + 18} dump ram_{controls}_step{i}"]
            t += 20
        lines += [
            f"{t} dump ram_{controls}_recognized",
            f"{t + 40} dump ram_{controls}_ready",
            f"{t + 42} button down 1",
            f"{t + 48} button up 1",
            f"{t + 57} dump ram_{controls}_projectile15",
            f"{t + 67} dump ram_{controls}_projectile",
            f"{t + 82} dump ram_{controls}_projectile40",
            f"{t + 90} button up 2",
            f"{t + 110} dump ram_{controls}_released",
        ]
        t += 150
    lines.append(f"{t} quit")
    return "\n".join(lines) + "\n"


def hashes(root):
    return {
        p.relative_to(root).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
        for p in root.rglob("*")
        if p.is_file()
    }


def run(args):
    registry = json.loads((ROOT / "scripts/buildtool/regions.json").read_text(encoding="utf-8"))["regions"][args.region]
    profile = json.loads((ROOT / registry["runtime_profile"]).read_text(encoding="utf-8"))
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", args.name):
        raise ValueError("Use a simple run name")
    out = ROOT / "build/harness" / args.name
    out.mkdir(parents=True, exist_ok=False)
    exe = ROOT / "dist" / ("DrakanPC" if args.region == "pal" else f"DrakanPC-{args.region}") / "drakan.exe"
    source_hashes = hashes(args.save) if args.save else None
    if args.save:
        if any(not p.name.startswith(registry["save_id"]) for p in args.save.iterdir() if p.is_dir()):
            raise ValueError("Use a native save for the selected disc; saves cannot be renamed across releases")
        shutil.copytree(args.save, out / "card/mc0")
    else:
        (out / "card/mc0").mkdir(parents=True)
    ini = patch_ini(
        (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
        {
            "general": {
                "title_menu": "native",
                "pause_menu": "native",
                "save_menu": "native",
                "inventory_screen": "native",
                "pause_when_unfocused": "0",
            },
            "video": {"world_rate": "30"},
        },
    )
    (out / "drakan.ini").write_text(ini, encoding="utf-8")
    input_ini = patch_ini(
        (exe.parent / "drakan_input.ini").read_text(encoding="utf-8"),
        {
            "general": {"look_mode": "direct"},
            "mouse": {
                "dragon_mode": "stick",
                "invert_x": "0",
                "invert_y": "0",
                "sensitivity_x": "0.002",
                "sensitivity_y": "0.002",
                "free_combat": "1",
            },
            "keyboard": {"strafe": "1", "toggle_run": "0"},
        },
    )
    (out / "input.ini").write_text(input_ini, encoding="utf-8")
    if args.discover:
        (out / "recipe.txt").write_text(discovery_recipe(), encoding="utf-8", newline="\n")
    elif args.from_ram:
        (out / "recipe.txt").write_text(gesture_recipe(args.sequence), encoding="utf-8", newline="\n")
    else:
        shutil.copyfile(args.recipe, out / "recipe.txt")
    env = dict(
        os.environ,
        DRAKAN_INI=str(out / "drakan.ini"),
        DRAKAN_INPUT_INI=str(out / "input.ini"),
        DRAKAN_MEMCARD_DIR=str(out / "card/mc0"),
        DRAKAN_NATIVE_OPTIONS_INI=str(out / "native.ini"),
        DRAKAN_INPUT_SCRIPT=str(out / "recipe.txt"),
        DRAKAN_SHOT_DIR=str(out),
        DRAKAN_LOG_DIR=str(out),
        DRAKAN_OVERLAY_SHOT_DIR=str(out),
        DRAKAN_NO_ACTIVATE="1",
        DRAKAN_PAUSE_UNFOCUSED="0",
        DRAKAN_AUDIO="0",
        DRAKAN_CONSOLE="0",
        PS2X_GS="d3d11",
        PS2X_MAX_SECONDS=str(args.timeout),
        DRAKAN_WORLD_RATE="30",
        DRAKAN_AUTOPRESS="2000",
        DRAKAN_AUTOPRESS_UNTIL="8000" if args.save else "100000",
        DRAKAN_INPUT_AUTOSTART="0" if args.save else "1",
    )
    if args.trace_release:
        env["DRAKAN_TRACE_FN"] = "38cca0,397650,398370"
        env["DRAKAN_TRACE_N"] = "1000"
    if args.level:
        env["DRAKAN_START_LEVEL"] = args.level
    with ThreadPoolExecutor(max_workers=1) as pool:
        future = pool.submit(run_once, exe, env, out, args.timeout + 20, 1)
        if args.save:
            driver = Driver(out)
            driver.expect(open=0, screen=profile["input_flow"]["title_screen"])
            driver.command("pad CROSS")
            driver.expect(open=0, screen=4)
            for _ in range(30):
                time.sleep(2)
                if future.done() or state(out).get("play") == 1:
                    break
                if state(out).get("screen") == 4:
                    driver.command("pad CROSS")
        completed = future.result()
    evidence = {p.stem: snapshot(p, profile) for p in sorted(out.glob("ram*.ram"))} if completed else {}
    preserved = args.save is None or source_hashes == hashes(args.save)
    if args.discover:
        discovered = evidence.get("ram_discovery", {}).get("spells", [])
        checks = {"native_unlocked_spell_sequences": any(s["level"] and s["sequence"] for s in discovered)}
    else:
        checks = check_evidence(evidence, args.sequence)
    projectile_checks = {key: checks.pop(key) for key in list(checks) if key.endswith("_spell_launched")}
    report = {
        "projectile_checks": projectile_checks,
        "validation_scope": "fixture_discovery_only" if args.discover else "native_recognition_and_cast_acceptance",
        "projectile_validation": bool(projectile_checks) and all(projectile_checks.values()),
        "level": args.level,
        "profile_sha256": hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest(),
        "source_hashes": source_hashes,
        "check_mode": "fixture_discovery_only" if args.discover else "native_spell_gesture",
        "gesture_validation": not args.discover,
        "checks": checks,
        "region": args.region,
        "script_completed": completed,
        "source_unchanged": preserved,
        "samples": evidence,
    }
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "samples"}), flush=True)
    return 0 if completed and preserved and evidence and all(checks.values()) else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--region", choices=("pal",), default="pal")
    parser.add_argument("--save", type=Path, required=True)
    parser.add_argument("--level")
    parser.add_argument("--recipe", type=Path)
    parser.add_argument(
        "--discover", action="store_true", help="Native shaman/input setup; discovers sequences, no gameplay pass"
    )
    parser.add_argument("--from-ram", type=Path, help="Generate input recipe from a discovered native RAM spell table")
    parser.add_argument("--spell-index", type=int, default=0)
    parser.add_argument("--trace-release", action="store_true", help="Bounded native release/projectile function trace")
    parser.add_argument("--sequence", help="Known native spell gesture, e.g. ULR; U/D/L/R")
    parser.add_argument("--name", required=True)
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    if args.from_ram:
        profile = json.loads((ROOT / "configs/regions/pal.json").read_text(encoding="utf-8"))
        spells = snapshot(args.from_ram, profile)["spells"]
        spell = next((s for s in spells if s["index"] == args.spell_index and s["level"]), None)
        if not spell or not spell["sequence"]:
            parser.error("Selected spell has no learned native direction sequence in the supplied dump")
        args.sequence = spell["sequence"]
    if not args.discover:
        if not args.recipe and not args.from_ram:
            parser.error("Use --discover, --from-ram or --recipe")
        if not args.sequence or not re.fullmatch("[UDLR]{2,10}", args.sequence):
            parser.error("Provide a valid 2..10 U/D/L/R native spell sequence")
        if not ({"L", "R"} <= set(args.sequence)):
            parser.error("This paired horizontal regression requires a spell containing both L and R")
    return run(args)


if __name__ == "__main__":
    raise SystemExit(main())
