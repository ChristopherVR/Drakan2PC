"""Direct-dist menu-hosted City native pickup regression. Reserve live slot before --run.

Uses native Use input on real pickups (or an explicit guest-teleport GOTO aid), never synthetic TAKE. Private settings/cards.
"""

from __future__ import annotations

import argparse
import configparser
import hashlib
import json
import os
import re
import struct
import subprocess
import time
from pathlib import Path

from arena_movement import host_recipe
from arena_two_instances import guest_progress_checks
from netplay_menu_two_instances import recipe_b_text

ROOT = Path(__file__).resolve().parents[2]

# Each pair keeps the stock pickup entries. Only the two players' starting poses change.
# The street pair uses City spawn points 1 and 2, facing their nearby mana pickups.
CITY_SITES = {
    "blade-mana": ((0, "0x3373c", "157758 170900 470120 0.01"), (1, "0x31b33", "198101 168300 426235 0.01")),
    "street-mana": ((2, "0x31b33", "176268 171500 458989 90"), (3, "0x31b33", "190070 170000 460163 90")),
}


def city_site_map(sites: str) -> str:
    source = (ROOT / "configs/mp_maps.ini").read_text(encoding="utf-8")
    section = source.split("[arena city-skeleton-square]", 1)[1].split("[arena ", 1)[0]
    lines = [line for line in section.splitlines() if not line.startswith("spawn =")]
    return "\n".join(
        ["[arena city-skeleton-square]", *lines, *(f"spawn = {pose}" for _, _, pose in CITY_SITES[sites]), ""]
    )


NATIVE_OPTION_KEYS = {
    "sensitivity",
    "vibrate",
    "subtitles",
    "stereo",
    "auto_mount_dragon",
    "invert_y",
    "controller_scheme",
    "sound_volume",
    "music_volume",
    "difficulty",
    "screen_x_offset",
    "screen_y_offset",
}


def private_native_options(source: Path) -> str:
    text = source.read_text(encoding="utf-8")
    config = configparser.ConfigParser(interpolation=None, strict=False)
    config.read_string(text)
    fields = set(config["options"]) if config.has_section("options") else set()
    missing = NATIVE_OPTION_KEYS - fields
    if missing:
        raise ValueError(f"native options incomplete: {', '.join(sorted(missing))}")
    return text


def actions(native_use: bool = False, approach: bool = False) -> list[str]:
    if native_use:
        return [
            "4000 dump ram_before",
            "4000 shot before",
            *(
                [
                    "7000 key down W",
                    "7100 key tap E",
                    "7250 key tap E",
                    "7400 key tap E",
                    "7500 key up W",
                    "7600 key tap E",
                ]
                if approach
                else []
            ),
            "10000 key tap E",
            "11000 key tap E",
            "12000 key tap E",
            "13000 key tap E",
            "15000 dump ram_pickup",
            "15000 shot pickup",
            "17000 key tap TAB",
            "18500 shot inventory",
            "19000 key tap ENTER",
            "19500 dump ram_inventory",
            "20000 cursor 250 124",
            "20500 cursor 251 125",
            "21000 key tap ENTER",
            "22500 shot equip_request",
            "23000 key tap ENTER",
            "24000 key tap TAB",
            "25000 dump ram_equipped",
            "25000 shot equipped",
            "26000 key down W",
            "27000 key up W",
            "28000 dump ram_moved",
            "28500 dump ram_late",
            "29000 key tap TAB",
            "30000 cursor 250 124",
            "30500 shot late_inventory",
            "31000 dump ram_late_inventory",
            "31500 key tap ENTER",
            "32500 key tap TAB",
            "33000 button down 1",
            "33100 button up 1",
            "34000 key down S",
            "34500 key up S",
            "35000 dump ram_final",
            "35000 shot final",
        ]
    lines = [
        "4000 dump ram_before",
        "4000 shot before",
        "22000 dump ram_pickup",
        "22000 shot pickup",
        "28000 key tap TAB",
        "30000 shot inventory",
        "30200 key tap ENTER",
        "31000 dump ram_inventory",
        "32000 cursor 250 124",
        "33000 cursor 251 125",
        "34000 key tap ENTER",
        "35500 shot equip_request",
        "36000 key tap TAB",
        "38000 dump ram_equipped",
        "38000 shot equipped",
        "42000 key down W",
        "43000 key up W",
        "44000 dump ram_moved",
        "65000 dump ram_late",
        "65000 shot late",
        "70000 key tap TAB",
        "71500 cursor 250 124",
        "72000 shot late_inventory",
        "73000 dump ram_late_inventory",
        "73500 key tap ENTER",
        "74500 shot late_equip_request",
        "75000 key tap TAB",
        "78000 button down 1",
        "78100 button up 1",
        "82000 key down S",
        "83000 key up S",
        "85000 dump ram_final",
        "85000 shot final",
    ]
    return lines


def pickup_host_recipe(arena: int, skin_steps: int, native_use: bool = False, approach: bool = False) -> str:
    recipe = host_recipe(arena, skin_steps)
    prefix = recipe[: recipe.index("10000 dump ram_base")]
    prefix = (
        prefix.replace("35000 shot", "59000 shot").replace("36000 key", "60000 key").replace("36500 key", "60500 key")
    )
    return prefix + "\n".join([*actions(native_use, approach), f"{38000 if native_use else 95000} quit"]) + "\n"


def pickup_client_recipe(native_use: bool = False, approach: bool = False) -> str:
    lines = recipe_b_text("\n".join(actions(native_use, approach)), 38000 if native_use else 95000).splitlines()
    for index, line in enumerate(lines):
        if "sync play" in line:
            break
        fields = line.split(" ", 1)
        if fields[0].isdigit() and int(fields[0]) > 0:
            lines[index] = f"{int(fields[0]) + 20000} {fields[1]}"
    return "\n".join(lines) + "\n"


def inventory(path: Path) -> dict:
    data = path.read_bytes()

    def rd(address: int) -> int:
        address &= 0x1FFFFFFF
        if address < 0x100000 or address + 4 > len(data):
            raise ValueError(f"bad guest pointer {address:x}")
        return struct.unpack_from("<I", data, address)[0]

    ctx = rd(0x5565D4)
    actor = rd(ctx + 0x4B0)
    player = rd(rd(actor + 0xFC) + 0x10)
    res = rd(ctx + 0x320)
    table = rd(res + 8)
    count = rd(res + 0x10)

    def item(handle: int) -> dict:
        if handle <= 0x53:
            return dict(handle=handle, sentinel=bool(handle))
        index = rd(handle + 0x10) & 0xFFFF
        record = table + index * 0x24
        return dict(
            handle=hex(handle),
            type=rd(handle + 0xC),
            resource=hex(rd(record)) if index <= count else None,
            class_id=hex(rd(record + 8)) if index <= count else None,
            props=hex(rd(handle + 0x54)),
        )

    cells = []
    sentinels = []
    for i in range(24):
        cell = player + 0x584 + 0x30 + i * 16
        h = rd(cell + 8)
        if 0 < h <= 0x53:
            # Native spell/hotkey cells contain small integer tags, not actor pointers.
            # The runtime's armTick applies the same <=0x53 guard.
            sentinels.append(dict(index=i, value=h))
        elif h:
            cells.append(dict(index=i, equipped=rd(cell), hot=rd(cell + 4), **item(h)))
    world_items = []
    registry, high = rd(ctx + 0x7C), rd(ctx + 0x84)
    if high > 65535:
        raise ValueError("invalid registry high-water mark")
    for slot in range(high + 1):
        handle = rd(registry + slot * 8)
        if not 0x100000 <= handle < len(data) - 0x120:
            continue
        if rd(handle) != 0x51B8B0 or rd(handle + 0x10) >> 16 != slot + 1:
            continue
        if rd(handle + 0xC) not in (10, 11):
            continue
        entry = item(handle)
        if entry["resource"] not in ("0x3373c", "0x31b33"):
            continue
        world_items.append(
            dict(
                **entry,
                cell=struct.unpack("<i", struct.pack("<I", rd(handle + 0x7C)))[0],
                spatial_flags=hex(rd(handle + 0x104)),
                model_instance=hex(rd(handle + 0x50)),
                position=list(struct.unpack_from("<3f", data, handle + 0x14)),
            )
        )
    return dict(
        actor=hex(actor),
        cells=cells,
        sentinels=sentinels,
        world_items=world_items,
        hands=[item(rd(player + 0xA48 + i * 16)) for i in range(4)],
        position=list(struct.unpack_from("<3f", data, actor + 0x14)),
    )


def report(out: Path, pickup: int = 0, native_use: bool = False, expected_resource: str = "0x3373c") -> dict:
    text = (out / "stdout_1.log").read_text(encoding="latin-1")
    snaps = {}
    errors = []
    for path in out.glob("ram_*.ram"):
        try:
            snaps[path.stem] = inventory(path)
        except (OSError, ValueError, struct.error) as error:
            errors.append(str(error))
    checks = guest_progress_checks(text)
    checks.update(
        native_options_initialized="[menus] native options prepared" in text,
        menu_launched="multiplayer launch: New Game with" in text,
        city_arena="arena world mode on: City of the Damned" in text,
        native_interaction=("key tap E" in (out / "recipe.txt").read_text())
        if native_use
        else f"test: going to pickup {pickup}" in text,
        no_synthetic_take="vanishes the way" not in text,
        snapshots_complete=len(snaps) == 8,
        inventory_open="gui screen -1 -> 21" in text,
    )
    checks["claim_sent"] = re.search(rf"pickup {pickup} \(.*\) taken here: claim", text) is not None
    checks["no_null_item_props"] = all(
        c["props"] != "0x0" for snap in snaps.values() for c in snap["cells"] if c["type"] in (10, 11)
    )
    before = snaps.get("ram_before", {})
    initial = {cell["resource"] for cell in before.get("cells", [])}
    gained = sorted(
        {
            cell["resource"]
            for name, snap in snaps.items()
            if name != "ram_before"
            for cell in snap["cells"]
            if cell["resource"] not in initial and cell["resource"]
        }
    )
    item_check = "blade_in_native_inventory" if expected_resource == "0x3373c" else "mana_in_native_inventory"
    expected_type = 10 if expected_resource == "0x3373c" else 11
    checks[item_check] = any(
        cell["resource"] == expected_resource and cell["type"] == expected_type and cell["props"] != "0x0"
        for snap in snaps.values()
        for cell in snap["cells"]
    )
    checks["valid_equipped_hand_after_pickup"] = any(
        hand.get("type") == 10 and hand.get("props") != "0x0"
        for name in ("ram_equipped", "ram_final")
        for hand in snaps.get(name, {}).get("hands", [])
    )
    if "ram_equipped" in snaps and "ram_moved" in snaps:
        a, b = snaps["ram_equipped"]["position"], snaps["ram_moved"]["position"]
        checks["native_movement_after_inventory"] = sum((b[i] - a[i]) ** 2 for i in (0, 2)) > 100**2
    else:
        checks["native_movement_after_inventory"] = False
    result = dict(checks=checks, snapshots=snaps, errors=errors, gained_resources=gained)
    result["passed"] = all(checks.values()) and not errors
    (out / "pickup.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def peer_pickup_checks(host: str, client: str, client_pickup: int = 0, host_pickup: int = 0) -> dict[str, bool]:
    return {
        f"server_accepted_player_{pid}": re.search(
            rf"server: pickup {host_pickup if pid == 1 else client_pickup} \(.*\) taken by player {pid}", host
        )
        is not None
        for pid in (1, 2)
    } | {
        "host_removed_client_copy": re.search(rf"pickup {client_pickup} \(.*\) taken by player 2: removed here", host)
        is not None,
        "client_removed_host_copy": re.search(rf"pickup {host_pickup} \(.*\) taken by player 1: removed here", client)
        is not None,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name", default="issue1-city-native-pickup")
    parser.add_argument("--arena", type=int, choices=range(3), default=1)
    parser.add_argument("--skin-steps", type=int, choices=range(16), default=0)
    parser.add_argument("--joined", action="store_true")
    parser.add_argument("--interaction", choices=("goto", "use"), default="goto")
    parser.add_argument("--maps-file", type=Path)
    parser.add_argument(
        "--city-sites",
        choices=CITY_SITES,
        help="Host/join City at the authored pickup sites using native movement and Use",
    )
    parser.add_argument("--look", choices=("direct", "stick"), default="direct")
    parser.add_argument("--strafe", type=int, choices=(0, 1), default=1)
    parser.add_argument("--run", action="store_true")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.city_sites:
        if args.maps_file or args.arena != 1:
            parser.error("--city-sites requires the stock City arena")
        args.joined = True
        args.interaction = "use"
    targets = CITY_SITES.get(args.city_sites, ((0, "0x3373c", ""), (int(args.interaction == "use"), "0x3373c", "")))
    if args.report:
        index, resource, _ = targets[int(args.report.resolve().name == "client")]
        result = report(
            args.report.resolve(),
            index,
            args.interaction == "use",
            resource,
        )
        print(json.dumps(result, indent=2))
        return 0 if result["passed"] else 1
    out = ROOT / "build/harness" / args.name
    if out.resolve().parent != (ROOT / "build/harness").resolve():
        parser.error("--name must be a single folder name")
    if out.exists():
        parser.error("output already exists; choose a fresh --name")
    exe = ROOT / "dist/DrakanPC/drakan.exe"
    processes = []
    deadline = time.monotonic() + 240
    logs = []
    try:
        for role in ("host", "client") if args.joined else ("host",):
            folder = out / role
            folder.mkdir(parents=True)
            (folder / "card/mc0").mkdir(parents=True)
            main_config = (exe.parent / "drakan.ini").read_text(encoding="utf-8")
            for key in ("title_menu", "pause_menu", "save_menu", "inventory_screen"):
                main_config = re.sub(rf"(?m)^{key}\s*=.*$", f"{key} = modern", main_config)
            (folder / "drakan.ini").write_text(main_config, encoding="utf-8")
            (folder / "multiplayer.ini").write_text(
                f"name = {role}\ncolour = 0\nmodel = 0\ncharacter = 0\n", encoding="utf-8"
            )
            (folder / "native_options.ini").write_text(
                private_native_options(exe.parent / "drakan_native_options.ini"), encoding="utf-8"
            )
            recipe = (
                pickup_host_recipe(args.arena, args.skin_steps, args.interaction == "use", bool(args.city_sites))
                if role == "host"
                else pickup_client_recipe(args.interaction == "use", bool(args.city_sites))
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
                DRAKAN_MP_PORT="47989",
                PS2X_MAX_SECONDS="300",
                DRAKAN_ARENA_PICKUP_SEC="8",
                DRAKAN_ARENA_SHOTS="1",
            )
            if args.interaction == "goto":
                env["DRAKAN_ARENA_TEST_GOTO"] = "0@1800" if role == "host" else "0@5000"
            if args.maps_file:
                env["DRAKAN_MP_MAPS"] = str(args.maps_file.resolve())
            if args.city_sites:
                maps = folder / "city.ini"
                maps.write_text(city_site_map(args.city_sites), encoding="utf-8")
                env["DRAKAN_MP_MAPS"] = str(maps)
                del env["DRAKAN_ARENA_PICKUP_SEC"]
            manifest = dict(
                executable=str(exe),
                sha256=hashlib.sha256(exe.read_bytes()).hexdigest(),
                arguments=vars(args) | {"report": None, "maps_file": str(args.maps_file) if args.maps_file else None},
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
            codes = [process.wait(timeout=max(1, deadline - time.monotonic())) for process in processes]
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
    results = {
        role: report(out / role, targets[i][0], args.interaction == "use", targets[i][1])
        for i, role in enumerate(("host", "client") if args.joined else ("host",))
    }
    network = (
        peer_pickup_checks(
            (out / "host/stdout_1.log").read_text(encoding="latin-1"),
            (out / "client/stdout_1.log").read_text(encoding="latin-1"),
            targets[1][0],
            targets[0][0],
        )
        if args.joined
        else {}
    )
    (out / "summary.json").write_text(
        json.dumps(dict(exit_codes=codes, roles=results, network=network), indent=2) + "\n", encoding="utf-8"
    )
    return (
        0
        if all(code == 0 for code in codes) and all(r["passed"] for r in results.values()) and all(network.values())
        else 1
    )


if __name__ == "__main__":
    raise SystemExit(main())
