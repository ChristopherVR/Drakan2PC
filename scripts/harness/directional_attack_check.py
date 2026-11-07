"""Compare native WASD attack animations with arrow controls using a private native card.

Run from the deployed play folder through disc_combat_check; no desktop input is sent.
An ordinary attack matching both controls is deliberately insufficient to pass.
With the default [keyboard] directional_attack = modifier, the "wasd" control holds Left Alt
(DIRECTIONAL_ATTACK) and must match the arrow move; the "plain" control holds only the movement
key and must stay an ordinary moving attack that keeps Rynn moving.
"""

from __future__ import annotations

import argparse
import hashlib
import itertools
import json
import math
import re
import struct
from pathlib import Path
from types import SimpleNamespace

import disc_combat_check as combat

DIRECTIONS = {"up": ("UP", "W"), "down": ("DOWN", "S"), "left": ("LEFT", "A"), "right": ("RIGHT", "D")}
ATTACK_FRAMES = tuple(range(1, 42, 4))
MODIFIER = "LALT"
CONTROLS = ("arrow", "wasd", "plain")
PLAIN_HOLD = 60  # frames the plain control keeps walking, through the whole attack window


def native_pose(path):
    """PAL skeleton channel reads matching world::readPlayerPose (Pose.cpp/Pose.h).

    AEC is a locomotion selector, not an attack clip. Node 2's channel carries the
    actual upper-body clip, including moving attacks. All reads are bounds checked.
    """
    data = path.read_bytes()
    if len(data) != 0x2000000:
        raise ValueError("Expected complete 32 MiB guest RAM")

    def read(fmt, address):
        address &= 0x1FFFFFFF
        if not 0x100000 <= address <= len(data) - struct.calcsize(fmt):
            raise ValueError(f"Invalid native pose address {address:#x}")
        return struct.unpack_from(fmt, data, address)[0]

    u32 = lambda address: read("<I", address)
    context = u32(0x5565D4)
    handle = u32(context + 0x4B0)
    player = u32(u32(handle + 0xFC) + 0x10)
    skeleton = u32(handle + 0x50)
    channels, count, table = u32(skeleton + 0xE8), u32(skeleton + 0xF0), u32(skeleton + 0xDC)
    if not 0 < count <= 32:
        raise ValueError("Native skeleton channel count unavailable")
    result = {
        "attacking": bool(u32(player + 0x94) & 0x80),
        "right_weapon_ref": read("<H", player + 0xED0),
        "right_holder_mode": u32(player + 0xEB4),
        "left_holder_mode": u32(player + 0xE90),
    }
    for name, node in (("lower", 0), ("upper", 2)):
        index = read("<h", table + node * 2)
        if not 0 <= index < count:
            raise ValueError(f"Native {name} channel unavailable")
        channel = channels + index * 0x80
        result[name] = {"clip": u32(channel + 0x50), "time": read("<f", channel + 0x58)}
        if not math.isfinite(result[name]["time"]):
            raise ValueError("Nonfinite native clip time")
    return result


def recipe(directions):
    lines = ["0 sync play 90", "2 key tap Q"]
    frame = 100
    for direction in ("neutral", *directions):
        if direction == "neutral":
            controls = (("neutral", None),)
        else:
            arrow, move = DIRECTIONS[direction]
            controls = (("arrow", arrow), ("wasd", move), ("plain", move))
        for control, key in controls:
            label = f"{direction}_{control}"
            lines.append(f"{frame} dump ram_{label}_base")
            if control == "wasd":
                lines.append(f"{frame + 2} key down {MODIFIER}")
            if key:
                lines.append(f"{frame + 2} key down {key}")
            lines.append(f"{frame + 10} dump ram_{label}_move")
            lines.append(f"{frame + 12} button down 1")
            for offset in ATTACK_FRAMES:
                lines.append(f"{frame + 12 + offset} dump ram_{label}_attack_{offset:02}")
                if offset in (5, 17):
                    lines.append(f"{frame + 12 + offset} shot {label}_attack_{offset:02}")
            lines.append(f"{frame + 18} button up 1")
            if key:
                lines.append(f"{frame + (PLAIN_HOLD if control == 'plain' else 20)} key up {key}")
            if control == "wasd":
                lines.append(f"{frame + 20} key up {MODIFIER}")
            lines.extend([f"{frame + 100} dump ram_{label}_released", f"{frame + 120} dump ram_{label}_idle"])
            frame += 140
    # Resume ordinary WASD movement after every attack/release check.
    lines.extend(
        [
            f"{frame} dump ram_movement_before",
            f"{frame + 2} key down W",
            f"{frame + 22} key up W",
            f"{frame + 24} dump ram_movement_after",
            f"{frame + 60} dump ram_movement_idle1",
            f"{frame + 80} dump ram_movement_idle2",
            f"{frame + 90} quit",
        ]
    )
    # Script executes events in time order; generated attack release lies between samples.
    return "\n".join(sorted(lines, key=lambda line: int(line.split()[0]))) + "\n"


def evaluate(samples, directions):
    checks, traces = {}, {}
    try:
        for direction in ("neutral", *directions):
            for control in ("neutral",) if direction == "neutral" else CONTROLS:
                label = f"{direction}_{control}"
                base, move, released, idle = (
                    samples[f"ram_{label}_{phase}"] for phase in ("base", "move", "released", "idle")
                )
                active = [samples[f"ram_{label}_attack_{offset:02}"] for offset in ATTACK_FRAMES]
                poses = [s["pose"] for s in active]
                attacks = {p["upper"]["clip"] for p in poses if p["attacking"]}
                traces[label] = {"poses": poses, "attack_only": sorted(attacks)}
                checks[f"{label}_armed_alive"] = all(
                    s["pose"]["right_weapon_ref"] not in (0, 0xFFFF)
                    and s["pose"]["right_holder_mode"] != 0xFFFFFFFF
                    and s["pose"]["left_holder_mode"] != 5
                    and s["hp"] > 0
                    for s in (base, *active, idle)
                )
                checks[f"{label}_native_attack"] = bool(attacks)
                checks[f"{label}_clip_advances"] = any(
                    a["attacking"]
                    and b["attacking"]
                    and a["upper"]["clip"] == b["upper"]["clip"]
                    and b["upper"]["time"] > a["upper"]["time"]
                    for a, b in itertools.pairwise(poses)
                )
                checks[f"{label}_released"] = (
                    not released["pose"]["attacking"]
                    and not idle["pose"]["attacking"]
                    and released["pose"]["upper"]["clip"] == idle["pose"]["upper"]["clip"]
                    and idle["pose"]["upper"]["clip"] not in attacks
                )
                checks[f"{label}_idle_stable"] = math.dist(released["rynn"]["position"], idle["rynn"]["position"]) < 1
        neutral = set(traces["neutral_neutral"]["attack_only"])
        for direction in directions:
            arrow = set(traces[f"{direction}_arrow"]["attack_only"])
            wasd = set(traces[f"{direction}_wasd"]["attack_only"])
            checks[f"{direction}_directional_baseline"] = bool(arrow - neutral)
            checks[f"{direction}_same_native_move"] = bool((arrow & wasd) - neutral)
            checks[f"{direction}_no_extra_attack"] = wasd == arrow
            # Without the modifier, held movement is an ordinary attack and Rynn keeps moving through it.
            plain = set(traces[f"{direction}_plain"]["attack_only"])
            checks[f"{direction}_plain_not_directional"] = not (plain & (arrow - neutral))
            first = samples[f"ram_{direction}_plain_attack_{ATTACK_FRAMES[0]:02}"]["rynn"]["position"]
            last = samples[f"ram_{direction}_plain_attack_{ATTACK_FRAMES[-1]:02}"]["rynn"]["position"]
            checks[f"{direction}_plain_keeps_moving"] = math.dist(first, last) > 10
        pos = lambda label: samples[label]["rynn"]["position"]
        checks["movement_after_attack"] = math.dist(pos("ram_movement_before"), pos("ram_movement_after")) > 10
        checks["movement_release"] = math.dist(pos("ram_movement_idle1"), pos("ram_movement_idle2")) < 1
    except KeyError as error:
        checks["required_samples"] = False
        traces["missing"] = str(error)
    return checks, traces


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--save", type=Path, required=True, help="native PAL mc0 folder; source is never modified")
    parser.add_argument("--name", required=True)
    parser.add_argument("--direction", choices=DIRECTIONS, action="append", help="default: all four")
    parser.add_argument("--timeout", type=int, default=300)
    parser.add_argument("--analyze", type=Path, help="re-evaluate an existing run report without launching")
    args = parser.parse_args()
    if not re.fullmatch(r"[a-zA-Z0-9_-]+", args.name):
        parser.error("Use a simple run name")
    directions = args.direction or list(DIRECTIONS)
    out = args.analyze or combat.ROOT / "build/harness" / args.name
    exe = combat.ROOT / "dist/DrakanPC/drakan.exe"
    if not args.analyze:
        generated = combat.ROOT / "build/harness" / f"{args.name}-recipe.txt"
        generated.parent.mkdir(parents=True, exist_ok=True)
        generated.write_text(recipe(directions), encoding="utf-8", newline="\n")
        exe_hash = hashlib.sha256(exe.read_bytes()).hexdigest()
        combat.run(
            SimpleNamespace(
                region="pal",
                save=args.save.resolve(),
                name=args.name,
                recipe=generated,
                timeout=args.timeout,
                level=None,
                check="observe",
            )
        )
    else:
        previous = out / "directional-report.json"
        exe_hash = json.loads(previous.read_text(encoding="utf-8"))["exe_sha256"] if previous.exists() else None
    report = json.loads((out / "report.json").read_text(encoding="utf-8"))
    for label, sample in report["samples"].items():
        sample["pose"] = native_pose(out / f"{label}.ram")
    checks, traces = evaluate(report["samples"], directions)
    result = {
        "exe_sha256": exe_hash,
        "directions": directions,
        "script_completed": report["script_completed"],
        "source_unchanged": report["source_unchanged"],
        "checks": checks,
        "traces": traces,
        "native_samples": {label: sample["pose"] for label, sample in report["samples"].items()},
    }
    (out / "directional-report.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    summary = {key: value for key, value in result.items() if key not in ("native_samples", "traces")}
    summary["attack_clips"] = {
        label: trace.get("attack_only") for label, trace in traces.items() if isinstance(trace, dict)
    }
    print(json.dumps(summary, indent=2))
    return 0 if report["script_completed"] and report["source_unchanged"] and checks and all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
