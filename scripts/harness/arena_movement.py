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
import time
from pathlib import Path

from combat_ram_report import report as combat_report
from dual_wield_check import patch_ini
from netplay_menu_two_instances import ARENAS, host_keys, recipe_b_text, taps

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


def host_recipe(
    arena: int,
    skin_steps: int,
    start_ms: int = 36500,
    *,
    early_spawn: bool = False,
    from_save: bool = False,
    return_delay_ms: int = 5000,
) -> str:
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    if from_save:
        lines += singleplayer_return(return_delay_ms, from_save=True)
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


def client_recipe(
    delay_ms: int = 20000, from_save: bool = False, return_delay_ms: int = 5000, *, new_game: bool = False
) -> str:
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
    if from_save or new_game:
        # A fresh campaign needs time for AutoPress to finish the intro before pausing.
        lines[2:2] = singleplayer_return(95000 if new_game else return_delay_ms, from_save=from_save)
    return "\n".join(lines) + "\n"


def singleplayer_return(delay_ms: int, *, from_save: bool) -> list[str]:
    # With a populated card the initial focus is New Game, two rows below Continue.
    enter = (
        ["1400 shot continue_title", "1700 key tap UP", "2050 key tap UP", "2500 key tap ENTER"]
        if from_save
        else ["1500 key tap ENTER"]
    )
    quit_keys, confirm = taps(1000, ["DOWN"] * 4 + ["ENTER"], 750)
    return [
        *enter,
        "0 sync play 60",
        f"{delay_ms} shot singleplayer_loaded",
        f"{delay_ms + 1000} key tap ESC",
        "0 sync screen 23 8",
        *quit_keys,
        f"{confirm + 500} key tap ENTER",
        "0 sync screen 8 3",
        "400 shot returned_to_title",
        # After Continue, skip Load Game and New Game to recover the normal menu route.
        *(["700 key tap DOWN", "1050 key tap DOWN"] if from_save else []),
    ]


def transition_progress(text: str, role: str, expected_level: str, *, from_save: bool = True) -> str:
    """Recognize only ordered receipts from this trip through the two native loads."""
    action = "HostMatch" if role == "host" else "JoinMatch"
    stages = (
        [
            (r"title action Continue\(1\)", "continue_missing"),
            (r"afterLevelLoad [^\n]*fromSave=1 ok=1", "singleplayer_load_incomplete"),
        ]
        if from_save
        else [
            (r"afterLevelLoad level='Level 0 - Intro' path=[^\n]*fromSave=0 ok=1", "campaign_intro_load_incomplete"),
            (r"afterLevelLoad level='Level 1A - Surdana' path=[^\n]*fromSave=0 ok=1", "singleplayer_load_incomplete"),
        ]
    )
    stages += [
        (r"overlay: title session \d+ opened", "return_to_title_missing"),
        (f"multiplayer action {action}", "match_menu_action_missing"),
    ]
    pos = 0
    for pattern, missing in stages:
        match = re.search(pattern, text[pos:])
        if not match:
            return missing
        pos += match.end()
    launch = re.search(r"multiplayer launch: New Game with '([^\r\n]+)'", text[pos:])
    if not launch:
        return "match_launch_missing"
    if launch[1] != expected_level:
        return "unexpected_multiplayer_level"
    pos += launch.end()
    level = re.escape(expected_level)
    entered = re.search(rf"beforeLevelLoad level='{level}' path=[^\n]*fromSave=0", text[pos:])
    if not entered:
        return "multiplayer_load_entry_missing"
    pos += entered.end()
    finished = re.search(rf"afterLevelLoad level='{level}' path=[^\n]*fromSave=0 ok=(\d+)", text[pos:])
    if not finished:
        return "multiplayer_load_return_missing"
    if finished[1] != "1":
        return "multiplayer_load_failed"
    if "script quit" not in text[pos + finished.end() :]:
        return "post_load_script_incomplete"
    return "completed"


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
    execution_error = re.search(r"Error during program execution: ([^\r\n]+)", text)
    checks["no_execution_error"] = execution_error is None
    if execution_error:
        result["execution_error"] = execution_error[1]
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
        arguments = json.loads((out / "run.json").read_text(encoding="utf-8")).get("arguments", {})
        if arguments.get("joined"):
            _, _, match_log = text.partition("multiplayer launch: New Game with")
            checks["received_partner_pose_in_match"] = re.search(r"\[nettrace [\d.]+\] p\d+ ", match_log) is not None
        if renderer := arguments.get("renderer"):
            marker = "[gsr-webgpu] renderer on" if renderer == "webgpu" else "[gsr] D3D11 renderer on"
            checks["requested_renderer_active"] = marker in text
        from_save = bool(arguments.get("host_from_save" if out.name == "host" else "join_from_save"))
        if from_save or (out.name == "client" and arguments.get("join_from_new_game")):
            result["transition_progress"] = transition_progress(
                text, out.name, ARENAS[arguments["arena"]][1], from_save=from_save
            )
            checks["completed_singleplayer_transition"] = result["transition_progress"] == "completed"
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
    if result.get("transition_progress", "completed") != "completed":
        result["classification"] = result["transition_progress"]
    if execution_error:
        result["classification"] = "program_execution_error"
    (out / "movement.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="issue1-arena-movement")
    parser.add_argument("--arena", type=int, choices=range(3), default=2)
    parser.add_argument("--skin-steps", type=int, choices=range(16), default=0)
    parser.add_argument("--joined", action="store_true")
    client_start = parser.add_mutually_exclusive_group()
    client_start.add_argument(
        "--join-from-new-game", action="store_true", help="Play the campaign intro on a fresh card before joining"
    )
    client_start.add_argument(
        "--join-from-save", metavar="SLOT1", help="Copy this PAL slot 1 for the client to load before joining"
    )
    parser.add_argument("--host-from-save", metavar="SLOT1", help="Continue this PAL save before hosting")
    parser.add_argument(
        "--return-delay-ms", type=int, default=5000, help="Time at the loaded save before the pause sequence"
    )
    parser.add_argument(
        "--native-graphics",
        action="store_true",
        help="Use 1x rendering and original movies/textures, with PC lighting and upscaling disabled",
    )
    parser.add_argument("--renderer", choices=("d3d11", "webgpu"))
    parser.add_argument("--host-start-ms", type=int)
    parser.add_argument("--join-delay-ms", type=int)
    parser.add_argument("--look", choices=("direct", "stick"), default="direct")
    parser.add_argument("--trace-health", action="store_true", help="Bounded native health-write/damage-reducer trace")
    parser.add_argument("--strafe", type=int, choices=(0, 1), default=1)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.host_start_ms is None:
        args.host_start_ms = 60500
        if args.join_from_new_game:
            args.host_start_ms = 180000
        elif args.join_from_save and not args.host_from_save:
            args.host_start_ms = 95000
    if args.join_delay_ms is None:
        args.join_delay_ms = 20000
        if args.host_from_save and not (args.join_from_save or args.join_from_new_game):
            args.join_delay_ms = 45000
        elif args.join_from_save and not args.host_from_save:
            args.join_delay_ms = 10000
    if (args.join_from_save or args.join_from_new_game) and not args.joined:
        parser.error("a singleplayer-to-join route requires --joined")
    for slot in (args.host_from_save, args.join_from_save):
        if slot and not (Path(slot).is_dir() and Path(slot).name == "BESCES-50006Slot1"):
            parser.error("expected a PAL BESCES-50006Slot1 directory")
    if args.return_delay_ms < 0:
        parser.error("--return-delay-ms must be nonnegative")
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
    roles = ("host", "client") if args.joined else ("host",)
    timeouts = {}
    processes = []
    logs = []
    try:
        for role in roles:
            folder = out / role
            folder.mkdir(parents=True)
            (folder / "card/mc0").mkdir(parents=True)
            slot = args.host_from_save if role == "host" else args.join_from_save
            if slot:
                shutil.copytree(slot, folder / "card/mc0/BESCES-50006Slot1")
            main_config = (exe.parent / "drakan.ini").read_text(encoding="utf-8")
            for key in ("title_menu", "pause_menu", "save_menu", "inventory_screen"):
                main_config = re.sub(rf"(?m)^{key}\s*=.*$", f"{key} = modern", main_config)
            if args.native_graphics:
                main_config = patch_ini(
                    main_config,
                    {
                        "video": {
                            "scale": "1",
                            "hd_textures": "0",
                            "hd_movies": "0",
                            "texture_upscale": "1",
                            "dlss": "off",
                            "dlss_neural": "0",
                            "ray_shadows": "0",
                            "ray_gi": "0",
                            "ray_reflections": "0",
                            "modern_water": "0",
                        }
                    },
                )
            (folder / "drakan.ini").write_text(main_config, encoding="utf-8")
            (folder / "multiplayer.ini").write_text(
                f"name = {role}\ncolour = 0\nmodel = 0\ncharacter = 0\n", encoding="utf-8"
            )
            (folder / "native_options.ini").write_text(patch_ini("", {"options": NATIVE_OPTIONS}), encoding="utf-8")
            recipe = (
                host_recipe(
                    args.arena,
                    args.skin_steps,
                    args.host_start_ms,
                    early_spawn=True,
                    from_save=bool(slot),
                    return_delay_ms=args.return_delay_ms,
                )
                if role == "host"
                else client_recipe(
                    args.join_delay_ms, bool(slot), args.return_delay_ms, new_game=args.join_from_new_game
                )
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
                PS2X_WATCHDOG_DUMP_DIR=str(folder),
                PS2X_MAX_SECONDS="300",
            )

            if role == "client" and args.join_from_new_game:
                env.update(DRAKAN_AUTOPRESS="3000", DRAKAN_AUTOPRESS_UNTIL="90000")
            if args.renderer:
                env["PS2X_GS"] = args.renderer
            if args.trace_health:
                env["DRAKAN_TRACE_FN"] = "3d5710,314578"
                env["DRAKAN_TRACE_N"] = "200"
            manifest = dict(
                executable=str(exe),
                sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                arguments=vars(args) | {"report": None},
                environment={k: v for k, v in env.items() if k.startswith(("DRAKAN_", "PS2X_"))},
            )
            if args.run:
                log = (folder / "stdout_1.log").open("wb")
                logs.append(log)
                processes.append(
                    subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
                )
                manifest["pid"] = processes[-1].pid
            (folder / "run.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        if not args.run:
            print(f"Prepared {out}; no game launched. Use a fresh --name with --run after reserving live slot.")
            return 0
        try:
            deadline = time.monotonic() + 270
            codes = [process.wait(timeout=max(0, deadline - time.monotonic())) for process in processes]
        except subprocess.TimeoutExpired:
            from sp_mp_transition import capture_process

            codes = [process.poll() for process in processes]
            for role, process, code in zip(roles, processes, codes, strict=True):
                if code is None:
                    dump = out / role / "timeout-full.dmp"
                    try:
                        capture_process(process.pid, dump)
                        timeouts[role] = {"pid": process.pid, "dump": str(dump)}
                    except OSError as error:
                        timeouts[role] = {"pid": process.pid, "dump_error": str(error)}
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill()
                process.wait()
        for log in logs:
            log.close()
    results = {role: report(out / role) for role in roles}
    (out / "summary.json").write_text(
        json.dumps(dict(exit_codes=codes, timeouts=timeouts, roles=results), indent=2) + "\n", encoding="utf-8"
    )
    return 0 if all(code == 0 for code in codes) and all(r["passed"] for r in results.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
