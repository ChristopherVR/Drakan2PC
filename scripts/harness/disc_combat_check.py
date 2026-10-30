"""Profile-driven combat/flight RAM evidence using native disc fixtures and play_run.

No cross-release save conversion: --save must contain this disc's native card folders.
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

from dual_wield_check import patch_ini
from play_run import ROOT, run_once
from settings_check import Driver, state


def number(value):
    return int(value, 0) if isinstance(value, str) else value


def snapshot(path, profile):
    data = path.read_bytes()
    if len(data) != 0x2000000:
        raise ValueError("Expected a complete 32 MiB RAM dump")
    l = {k: number(v) for k, v in profile["camera"].items()}
    o = {k: number(v) for k, v in profile["combat_observer"].items()}

    def address(canonical):
        return canonical if profile["identity_addresses"] else number(profile["addresses"][f"0x{canonical:08x}"])

    def valid(a, n=4):
        return 0x100000 <= (a & 0x1FFFFFFF) <= len(data) - n

    def u32(a):
        return struct.unpack_from("<I", data, a & 0x1FFFFFFF)[0]

    def u16(a):
        return struct.unpack_from("<H", data, a & 0x1FFFFFFF)[0]

    def f32(a):
        return struct.unpack_from("<f", data, a & 0x1FFFFFFF)[0]

    def object_at(h):
        inner = u32(h + l["handle_inner"]) if valid(h, l["handle_inner"] + 4) else 0
        return u32(inner + l["inner_data"]) if valid(inner, l["inner_data"] + 4) else 0

    ctx = u32(address(0x5565D4))
    resources = u32(ctx + l["context_resources"])

    def resource_class(h):
        if not valid(h, l["handle_resource"] + 2) or not valid(resources, 20):
            return None
        index = u16(h + l["handle_resource"])
        if index == 0xFFFF or index > u32(resources + l["resource_count"]):
            return None
        entry = u32(resources + l["resource_table"]) + index * l["resource_stride"]
        return u32(entry + l["resource_class"]) if valid(entry, l["resource_stride"]) else None

    controlled = u32(ctx + l["context_actor"])
    table, count = u32(ctx + l["context_targets"]), u32(ctx + l["context_target_count"])
    if count > 10000 or not valid(table, (count + 1) * 8):
        raise ValueError("Actor registry unavailable")
    actors = {u32(table + i * 8): i + 1 for i in range(count + 1)}

    def actor(h):
        obj = object_at(h)
        return {
            "slot": actors.get(h),
            "class": resource_class(h),
            "position": [f32(h + l["transform_position"] + i * 4) for i in range(3)],
            "yaw": math.atan2(f32(h + l["transform_back"]), f32(h + l["transform_right"])),
            "pitch": math.atan2(
                -f32(h + o["transform_back_y"]),
                math.hypot(f32(h + l["transform_back"]), f32(h + o["transform_back_z"])),
            ),
            "flags": u32(obj + l["player_flags"]),
            "flags2": u32(obj + l["player_flags2"]),
        }

    rynns = [h for h in actors if resource_class(h) == o["rynn_class"]]
    if len(rynns) != 1:
        raise ValueError(f"Expected one Rynn, got {len(rynns)}")
    rynn = rynns[0]
    player = object_at(rynn)
    camera = object_at(u32(ctx + l["context_camera"]))
    key = u32(address(0x4FE86C))

    def stat(offset):
        encoded = u32(address(0x4FBAC8) + offset) ^ key
        return (encoded >> 8 | encoded << 24) & 0xFFFFFFFF

    result = {
        "controlled": actor(controlled),
        "rynn": actor(rynn),
        "dragons": [],
        "npcs": [],
        "walking": bool(u32(address(0x4FBB34)) & o["walk_mask"]),
        "hp": stat(o["stats_hp"]),
        "max_hp": stat(o["stats_max_hp"]),
        "pitch": f32(camera + l["camera_pitch"]),
        "camera_flags": u32(camera + l["camera_flags"]),
        "target": u16(player + l["player_target"]),
        "weapon": u16(player + o["player_active_weapon"]),
        "attack_state": u32(player + o["player_attack_state"]),
        "animation": u32(player + o["player_animation"]),
        "guard": u32(player + o["player_guard_flags"]),
    }
    for h, slot in actors.items():
        obj = object_at(h)
        if resource_class(h) == l["dragon_class"]:
            value = actor(h)
            value["state"] = u32(obj + o["dragon_state"])
            result["dragons"].append(value)
        if valid(obj, o["npc_max_health"] + 4) and u32(obj + o["npc_self"]) == h:
            value = actor(h)
            value.update(
                hp=struct.unpack_from("<i", data, (obj + o["npc_health"]) & 0x1FFFFFFF)[0],
                max_hp=u32(obj + o["npc_max_health"]),
            )
            value["distance"] = math.dist(value["position"], result["rynn"]["position"])
            if value["distance"] < 30000 and value["max_hp"] < 100000:
                result["npcs"].append(value)
    return result


def check_evidence(samples, profile, mode):
    def position(label):
        return samples[label]["controlled"]["position"]

    def turn(a, b):
        return math.remainder(samples[b]["controlled"]["yaw"] - samples[a]["controlled"]["yaw"], math.tau)

    def moved(a, b):
        return math.dist(position(a), position(b)) > 10

    if mode == "observe":
        return {}
    required = {
        "melee": ("ram_begin", "ram_target", "ram_block_held", "ram_block_up", "ram_end"),
        "movement": (
            "ram_base",
            "ram_run_active",
            "ram_run_end",
            "ram_walk_active",
            "ram_walk_end",
            "ram_strafe_end",
            "ram_look",
            "ram_idle",
        ),
        "flight": (
            "ram_start",
            "ram_neutral",
            "ram_climb",
            "ram_forward",
            "ram_turn",
            "ram_idle",
            "ram_primary",
            "ram_primary_up",
            "ram_secondary",
            "ram_secondary_up",
        ),
    }[mode]
    if not set(required) <= set(samples):
        return {"required_samples": False}
    o = profile["combat_observer"]
    if mode == "melee":
        target = samples["ram_target"]["target"]
        before = {n["slot"]: n["hp"] for n in samples["ram_begin"]["npcs"]}
        after = {n["slot"]: n["hp"] for n in samples["ram_end"]["npcs"]}
        mask = number(o["guard_block_mask"])
        return {
            "melee_equipped": samples["ram_target"]["weapon"] != 0,
            "target_locked": target != 0,
            "target_damaged": target in before and target in after and after[target] < before[target],
            "block_held": bool(samples["ram_block_held"]["guard"] & mask),
            "block_released": not bool(samples["ram_block_up"]["guard"] & mask),
            "survived": samples["ram_end"]["hp"] > 0,
        }
    if mode == "movement":
        return {
            "run_moves": moved("ram_base", "ram_run_end"),
            "walk_moves": moved("ram_run_end", "ram_walk_end"),
            "native_walk_pressed": samples["ram_walk_active"]["walking"],
            "native_walk_released": not samples["ram_walk_end"]["walking"],
            "native_run": not samples["ram_run_active"]["walking"],
            "strafe_moves": moved("ram_walk_end", "ram_strafe_end"),
            "strafe_preserves_heading": abs(turn("ram_walk_end", "ram_strafe_end")) < 0.02,
            "direct_mouse_yaw": abs(turn("ram_strafe_end", "ram_look") + 0.128) < 0.005,
            "idle_yaw_stable": abs(turn("ram_look", "ram_idle")) < 0.001,
        }

    def flags(label):
        return samples[label]["controlled"]["flags"]

    return {
        "mounted_throughout": all(
            s["controlled"]["class"] == number(profile["camera"]["dragon_class"]) for s in samples.values()
        ),
        "neutral_position_stable": math.dist(position("ram_start"), position("ram_neutral")) < 1,
        "climbed": position("ram_climb")[1] - position("ram_neutral")[1] > 100,
        "forward_moves": moved("ram_climb", "ram_forward"),
        "mouse_turns": abs(turn("ram_forward", "ram_turn")) > 0.01,
        "mouse_pitches": abs(samples["ram_turn"]["controlled"]["pitch"] - samples["ram_forward"]["controlled"]["pitch"])
        > 0.01,
        "walk_not_injected": all(not s["walking"] for s in samples.values()),
        # Native request delivery/release only; these do not prove breath damage.
        "primary_requested": bool(flags("ram_primary") & number(o["dragon_primary_mask"])),
        "primary_released": not bool(flags("ram_primary_up") & number(o["dragon_primary_mask"])),
        "secondary_requested": bool(flags("ram_secondary") & number(o["dragon_secondary_mask"])),
        "secondary_released": not bool(flags("ram_secondary_up") & number(o["dragon_secondary_mask"])),
    }


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
            "mouse": {
                "mode": "direct",
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
    checks = check_evidence(evidence, profile, args.check)
    report = {
        "level": args.level,
        "profile_sha256": hashlib.sha256(json.dumps(profile, sort_keys=True).encode()).hexdigest(),
        "source_hashes": source_hashes,
        "check_mode": args.check,
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
    parser.add_argument("--region", required=True)
    parser.add_argument("--save", type=Path)
    parser.add_argument("--level")
    parser.add_argument("--check", choices=("observe", "melee", "movement", "flight"), default="observe")
    parser.add_argument("--recipe", type=Path, required=True)
    parser.add_argument("--name", required=True)
    parser.add_argument("--timeout", type=int, default=300)
    return run(parser.parse_args())


if __name__ == "__main__":
    raise SystemExit(main())
