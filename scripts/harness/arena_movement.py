"""Menu-hosted arena W/S regression with optional joined client; dry-run unless --run.

Runs only dist/DrakanPC, with private cards/options/input files. Reserve the shared
live slot before --run. --report reads existing receipts without launching a game.
PAL guest addresses match runtime/input/include/input/CameraProfile.h.
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
import subprocess
from pathlib import Path

from combat_ram_report import report as combat_report
from dual_wield_check import patch_ini
from netplay_menu_two_instances import host_keys, recipe_b_text, taps

ROOT = Path(__file__).resolve().parents[2]
STAGES = ("base", "w", "w_release", "s", "s_release")
NATIVE_OPTIONS = {
    "sensitivity": "1",
    "vibrate": "1",
    "subtitles": "1",
    "stereo": "1",
    "auto_mount_dragon": "1",
    "invert_y": "0",
    "controller_scheme": "0",
    "sound_volume": "50",
    "music_volume": "25",
    "difficulty": "0",
    "screen_x_offset": "0",
    "screen_y_offset": "0",
}


def movement(early_spawn: bool = False) -> list[str]:
    early = ["1000 dump ram_spawn1", "3000 dump ram_spawn3", "5000 dump ram_spawn5"] if early_spawn else []
    return [
        *early,
        "10000 dump ram_base",
        "10000 shot base",
        "12000 key down W",
        "13000 dump ram_w",
        "14000 key up W",
        "16000 dump ram_w_release",
        "16000 shot forward",
        "18000 key down S",
        "19000 dump ram_s",
        "20000 key up S",
        "22000 dump ram_s_release",
        "22000 shot backward",
    ]


def host_recipe(arena: int, skin_steps: int, start_ms: int = 36500, *, early_spawn: bool = False) -> str:
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 1500
    groups = host_keys(arena_index=arena)
    if skin_steps:
        groups.insert(1, ["DOWN", "DOWN"] + ["RIGHT"] * skin_steps + ["UP", "UP"])
    for keys in groups:
        group, t = taps(t, keys, 250)
        lines += group
        t += 700
    lines += [
        f"{t} shot host_form",
        f"{t + 500} key tap ENTER",
        f"{start_ms - 1500} shot lobby",
        f"{start_ms - 500} key tap DOWN",
        f"{start_ms} key tap ENTER",
        "0 sync play 60",
    ]
    return "\n".join(lines + movement(early_spawn) + ["24000 quit"]) + "\n"


def client_recipe(delay_ms: int = 20000, from_save: bool = False) -> str:
    """Delay LAN discovery until the host has entered its lobby, before gameplay sync.

    This matches the successful joined arena_pickup R2/R3/R4 timing; movement
    timestamps after sync play stay relative to actual native gameplay.
    """
    lines = recipe_b_text("\n".join(movement(early_spawn=True)), 24000).splitlines()
    for index, line in enumerate(lines):
        if "sync play" in line:
            break
        fields = line.split(" ", 1)
        if fields[0].isdigit() and int(fields[0]) > 0:
            lines[index] = f"{int(fields[0]) + delay_ms} {fields[1]}"
    if from_save:
        # The title focuses New Game even when Continue and Load Game are available above it.
        quit_keys, _ = taps(7000, ["DOWN"] * 4 + ["ENTER"], 300)
        lines[2:2] = [
            "1400 shot continue_title",
            "1700 key tap UP",
            "2050 key tap UP",
            "2500 key tap ENTER",
            "3500 shot continue_requested",
            "0 sync play 60",
            "5000 shot singleplayer_loaded",
            "6000 key tap ESC",
            *quit_keys,
            "9300 key tap ENTER",
            "0 sync screen 8 3",
            "1000 shot returned_to_title",
            # The title remembers Continue after the load. Skip Load Game and New Game.
            "1300 key tap DOWN",
            "1600 key tap DOWN",
        ]
    return "\n".join(lines) + "\n"


def snapshot(path: Path) -> dict:
    data = path.read_bytes()
    if len(data) != 0x2000000:
        raise ValueError("Expected a complete 32 MiB guest RAM dump")

    def read(address: int, fmt: str = "I"):
        address &= 0x1FFFFFFF
        if address < 0x100000 or address + 4 > len(data):
            raise ValueError(f"invalid guest address {address:x}")
        return struct.unpack_from("<" + fmt, data, address)[0]

    def obj(handle: int) -> int:
        return read(read(handle + 0xFC) + 0x10)

    ctx = read(0x5565D4)
    handle = read(ctx + 0x4B0)
    controlled = bool(handle)
    combat = combat_report(path)
    if controlled:
        player = obj(handle)
        observed_handle = handle
    else:
        if not combat.get("rynn_available"):
            raise ValueError("Neither a controlled actor nor an observable native Rynn is available")
        player = int(combat["player"], 0)
        observed_handle = read(player + 0x80)
    camera_handle = read(ctx + 0x4B4)
    camera = obj(camera_handle) if camera_handle else 0
    resources = read(ctx + 0x320)
    resource_index = read(observed_handle + 0x10) & 0xFFFF
    if resource_index > read(resources + 0x10):
        raise ValueError("Controlled actor resource index exceeds native table")
    resource_record = read(resources + 8) + resource_index * 0x24
    return dict(
        controlled_actor_available=controlled,
        observed_handle=hex(observed_handle),
        hp=combat.get("hp"),
        resource_id=hex(read(resource_record)),
        class_id=hex(read(resource_record + 8)),
        handle=hex(handle),
        player=hex(player),
        resource=hex(read(observed_handle + 0x10)),
        position=[read(observed_handle + 0x14 + i * 4, "f") for i in range(3)],
        heading_deg=math.degrees(math.atan2(read(observed_handle + 0x38, "f"), read(observed_handle + 0x40, "f"))),
        basis=[read(observed_handle + 0x20 + i * 4, "f") for i in range(9)],
        native_y_fields={hex(offset): read(player + offset, "f") for offset in (0xE8, 0xBE8, 0xC24, 0xC30)},
        flags90=hex(read(player + 0x90)),
        flags94=hex(read(player + 0x94)),
        camera_flags=hex(read(camera + 0x48)) if camera else None,
        camera_mode=read(camera + 0x14C) if camera else None,
        summon=read(player + 0x1040),
        stick_map=[
            [read(0x4E9558 + i * 12), read(0x4E955C + i * 12, "f"), read(0x4E9560 + i * 12, "f")] for i in range(4)
        ],
    )


def report(out: Path) -> dict:
    result = {"snapshots": {}, "checks": {}}
    for stage in STAGES:
        path = out / f"ram_{stage}.ram"
        try:
            result["snapshots"][stage] = snapshot(path)
        except (OSError, ValueError, struct.error) as error:
            result.setdefault("errors", []).append(f"{stage}: {error}")
    result["spawn_snapshots"] = {}
    for stage in ("spawn1", "spawn3", "spawn5"):
        path = out / f"ram_{stage}.ram"
        if not path.exists():
            continue
        try:
            result["spawn_snapshots"][stage] = snapshot(path)
        except (OSError, ValueError, struct.error) as error:
            result.setdefault("spawn_errors", []).append(f"{stage}: {error}")
    shots = result["snapshots"]
    checks = result["checks"]
    text = (out / "stdout_1.log").read_text(encoding="latin-1")
    checks["menu_launched_native_level"] = "multiplayer launch: New Game with" in text
    checks["menu_host_or_join"] = any(f"multiplayer action {action}" in text for action in ("HostMatch", "JoinMatch"))
    checks["native_assigned_spawn"] = "[arena] player placed at spawn" in text
    checks["placement_settled"] = re.search(r"placement of spawn \d+ took after", text) is not None
    checks["native_armed"] = "[arena] armed: right hand holds actor" in text
    checks["script_completed"] = "script quit" in text
    if out.name == "client":
        checks["joined_client_menu_action"] = "multiplayer action JoinMatch" in text
    if (out / "run.json").exists():
        checks["native_options_prepared"] = "native options prepared for the game's normal startup loader" in text
        if out.name == "client" and json.loads((out / "run.json").read_text()).get("arguments", {}).get(
            "join_from_save"
        ):
            saved_load = re.search(r"afterLevelLoad [^\n]*fromSave=1 ok=1", text)
            join_at = text.find("multiplayer action JoinMatch")
            checks["joined_after_saved_game"] = bool(saved_load and saved_load.end() < join_at)
            checks["title_returned"] = (out / "returned_to_title.png").is_file()
    checks["all_snapshots"] = len(shots) == len(STAGES)
    observations = [*shots.values(), *result["spawn_snapshots"].values()]
    native_death = any(s.get("hp") is not None and s["hp"] <= 0 for s in observations)
    checks["controlled_actor_alive"] = bool(shots) and all(
        s["controlled_actor_available"] and s["hp"] is not None and s["hp"] > 0 for s in shots.values()
    )
    if checks["all_snapshots"]:
        base = shots["base"]
        checks["controlled_actor_alive"] = all(s["controlled_actor_available"] and s["hp"] > 0 for s in shots.values())
        checks["same_actor"] = all(s["handle"] == base["handle"] for s in shots.values())
        if (out / "run.json").exists():
            manifest = json.loads((out / "run.json").read_text(encoding="utf-8"))
            if manifest.get("arguments", {}).get("skin_steps") == 0:
                checks["default_rynn_actor"] = all(s["class_id"] == "0x10009" for s in shots.values())
        for name, start, end in (("w", "base", "w_release"), ("s", "w_release", "s_release")):
            delta = [b - a for a, b in zip(shots[start]["position"], shots[end]["position"], strict=True)]
            yaw = math.remainder(shots[end]["heading_deg"] - shots[start]["heading_deg"], 360)
            result[name] = dict(delta=delta, horizontal_distance=math.hypot(delta[0], delta[2]), yaw_delta=yaw)
            checks[f"{name}_translates"] = result[name]["horizontal_distance"] > 100
            checks[f"{name}_no_rotation"] = abs(yaw) < 1
        checks["opposite_directions"] = sum(result["w"]["delta"][i] * result["s"]["delta"][i] for i in (0, 2)) < 0
        checks["native_y_axis"] = all(s["stick_map"][1][0] == 2 for s in shots.values())
        checks["w_axis_positive"] = shots["w"]["stick_map"][1][1] > 0.5
        checks["s_axis_negative"] = shots["s"]["stick_map"][1][1] < -0.5
    result["passed"] = all(checks.values())
    result["classification"] = (
        "native_movement_pass"
        if result["passed"]
        else "native_controlled_actor_dead"
        if native_death
        else "missing_target_or_native_load_stall"
        if not checks["all_snapshots"]
        else "native_controlled_actor_dead_or_unavailable"
        if not checks.get("controlled_actor_alive", True)
        else "native_options_not_prepared"
        if not checks.get("native_options_prepared", True)
        else "native_spawn_or_loadout_incomplete"
        if not all(checks[key] for key in ("native_assigned_spawn", "placement_settled", "native_armed"))
        else "movement_or_routing_failure"
    )
    (out / "movement.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="issue1-arena-movement")
    parser.add_argument("--arena", type=int, choices=range(3), default=2)
    parser.add_argument("--skin-steps", type=int, choices=range(16), default=0)
    parser.add_argument("--joined", action="store_true")
    parser.add_argument(
        "--join-from-save", metavar="SLOT1", help="Copy this PAL slot 1 for the client to load before joining"
    )
    parser.add_argument("--host-start-ms", type=int)
    parser.add_argument("--join-delay-ms", type=int)
    parser.add_argument("--look", choices=("direct", "stick"), default="direct")
    parser.add_argument("--trace-health", action="store_true", help="Bounded native health-write/damage-reducer trace")
    parser.add_argument("--strafe", type=int, choices=(0, 1), default=1)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.host_start_ms is None:
        args.host_start_ms = 95000 if args.join_from_save else 60500
    if args.join_delay_ms is None:
        args.join_delay_ms = 10000 if args.join_from_save else 20000
    if args.join_from_save and (
        not args.joined
        or not (Path(args.join_from_save).is_dir() and Path(args.join_from_save).name == "BESCES-50006Slot1")
    ):
        parser.error("--join-from-save requires --joined and a PAL BESCES-50006Slot1 directory")
    if args.report:
        result = report(args.report.resolve())
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] else 1
    out = ROOT / "build/harness" / args.name
    if out.resolve().parent != (ROOT / "build/harness").resolve():
        parser.error("--name must be a single folder name")
    if out.exists():
        parser.error("output already exists; choose a fresh --name")
    exe = ROOT / "dist/DrakanPC/drakan.exe"
    processes = []
    logs = []
    try:
        for role in ("host", "client") if args.joined else ("host",):
            folder = out / role
            folder.mkdir(parents=True)
            (folder / "card/mc0").mkdir(parents=True)
            if role == "client" and args.join_from_save:
                shutil.copytree(args.join_from_save, folder / "card/mc0/BESCES-50006Slot1")
            main_config = (exe.parent / "drakan.ini").read_text(encoding="utf-8")
            for key in ("title_menu", "pause_menu", "save_menu", "inventory_screen"):
                main_config = re.sub(rf"(?m)^{key}\s*=.*$", f"{key} = modern", main_config)
            (folder / "drakan.ini").write_text(main_config, encoding="utf-8")
            (folder / "multiplayer.ini").write_text(
                f"name = {role}\ncolour = 0\nmodel = 0\ncharacter = 0\n", encoding="utf-8"
            )
            (folder / "native_options.ini").write_text(patch_ini("", {"options": NATIVE_OPTIONS}), encoding="utf-8")
            recipe = (
                host_recipe(args.arena, args.skin_steps, args.host_start_ms, early_spawn=True)
                if role == "host"
                else client_recipe(args.join_delay_ms, bool(args.join_from_save))
            )
            (folder / "recipe.txt").write_text(recipe, encoding="utf-8")
            config = (exe.parent / "drakan_input.ini").read_text(encoding="utf-8")
            for key, value in (("strafe", args.strafe), ("look_mode", args.look)):
                config = re.sub(rf"(?m)^{key}\s*=.*$", f"{key} = {value}", config)
            (folder / "input.ini").write_text(config, encoding="utf-8")
            env = {k: v for k, v in os.environ.items() if not k.startswith(("DRAKAN_", "PS2X_", "GSR_"))}
            env.update(
                DRAKAN_INI=str(folder / "drakan.ini"),
                DRAKAN_MULTIPLAYER_INI=str(folder / "multiplayer.ini"),
                DRAKAN_INPUT_SCRIPT=str(folder / "recipe.txt"),
                DRAKAN_INPUT_INI=str(folder / "input.ini"),
                DRAKAN_SHOT_DIR=str(folder),
                DRAKAN_LOG_DIR=str(folder),
                DRAKAN_MEMCARD_DIR=str(folder / "card/mc0"),
                DRAKAN_NATIVE_OPTIONS_INI=str(folder / "native_options.ini"),
                DRAKAN_GAME="d2",
                DRAKAN_LANG="english",
                DRAKAN_AUDIO="0",
                DRAKAN_PAUSE_UNFOCUSED="0",
                DRAKAN_INPUT_AUTOSTART="0",
                DRAKAN_AUTOPRESS="0",
                DRAKAN_NO_ACTIVATE="1",
                DRAKAN_FULLSCREEN="0",
                DRAKAN_NET_TRACE="1",
                DRAKAN_LIFECYCLE_LOG="1",
                DRAKAN_WINDOW_WIDTH="1280",
                DRAKAN_WINDOW_HEIGHT="720",
                DRAKAN_NET_NAME=role,
                PS2X_MAX_SECONDS="240",
            )

            if args.trace_health:
                env["DRAKAN_TRACE_FN"] = "3d5710,314578"
                env["DRAKAN_TRACE_N"] = "200"
            manifest = dict(
                executable=str(exe),
                sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                arguments=vars(args) | {"report": None},
                environment={k: v for k, v in env.items() if k.startswith(("DRAKAN_", "PS2X_"))},
            )
            (folder / "run.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
            if args.run:
                log = (folder / "stdout_1.log").open("wb")
                logs.append(log)
                processes.append(
                    subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
                )
        if not args.run:
            print(f"Prepared {out}; no game launched. Use a fresh --name with --run after reserving live slot.")
            return 0
        try:
            codes = [process.wait(timeout=270) for process in processes]
        except subprocess.TimeoutExpired:
            from sp_mp_transition import capture_process

            for role, process in zip(("host", "client"), processes, strict=False):
                if process.poll() is None:
                    capture_process(process.pid, out / role / "timeout-full.dmp")
            codes = [process.poll() for process in processes]
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
    results = {role: report(out / role) for role in (("host", "client") if args.joined else ("host",))}
    (out / "summary.json").write_text(
        json.dumps(dict(exit_codes=codes, roles=results), indent=2) + "\n", encoding="utf-8"
    )
    return 0 if all(code == 0 for code in codes) and all(r["passed"] for r in results.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
