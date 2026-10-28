"""Live dual-wield transition checks on an isolated copy of the magic5 card.

python scripts/harness/dual_wield_transitions.py --save build/harness/magic5/card/mc0

Requires bow cell 0, droppable club cell 1, non-droppable dagger cell 16. Uses the native
inventory/save screens, real save I/O, and the deployed executable. All cards, configs,
RAM and reports stay under build/harness. --case load expects a card saved while dual wielding.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from dataclasses import asdict
from pathlib import Path

import dual_wield_check as dw


def start(right=1, left=16):
    lines = dw.check_recipe(right, left, False).splitlines()
    lines = lines[: lines.index("6400 dump ram_04_left_equipped") + 1]
    return [s for s in lines if " dump " not in s or "04_left_equipped" in s]


def attacks(t=800):
    return [s for i in range(6) for s in dw.press(t + i * 600, "button 1")]


def recipe(case):
    if case == "reload":
        return (
            "\n".join(
                [
                    "clock milliseconds",
                    "1 log on 60",
                    "0 sync play 60",
                    *dw.press(300, "key TAB"),
                    "0 sync screen 21 20",
                    *dw.hover(800, 16),
                    *dw.press(1600, "key E"),
                    "3500 dump ram_changed",
                    *dw.press(3800, "key TAB"),
                    "0 sync play 60",
                    *dw.press(400, "key ESC"),
                    "1700 cursor 317 248",
                    "1800 cursor 318 249",
                    *dw.press(2200, "button 1"),
                    "0 sync screen 4 20",
                    "1200 cursor 179 159",
                    "1300 cursor 180 160",
                    *dw.press(1800, "button 1"),
                    "0 sync screen 27 20",
                    "700 shot load_confirm",
                    "800 cursor 253 292",
                    "900 cursor 254 293",
                    *dw.press(1200, "button 1"),
                    "0 sync play 60",
                    "800 dump ram_loaded",
                    "1000 shot loaded",
                    *attacks(1600),
                    "6000 dump ram_after_attacks",
                    "6200 shot after_attacks",
                    "6500 quit",
                ]
            )
            + "\n"
        )
    if case == "load":
        return (
            "\n".join(
                [
                    "clock milliseconds",
                    "1 log on 60",
                    "0 sync play 60",
                    "800 dump ram_loaded",
                    "1000 shot loaded",
                    *attacks(1600),
                    "6000 dump ram_after_attacks",
                    "6200 shot after_attacks",
                    "6500 quit",
                ]
            )
            + "\n"
        )
    lines = start(16, 1) if case == "drop-left" else start()
    if case == "save":
        lines += [
            *dw.press(6800, "key TAB"),
            "0 sync play 60",
            *dw.press(400, "key ESC"),
            "1700 cursor 317 214",
            "1800 cursor 318 215",
            *dw.press(2200, "button 1"),
            "4000 shot save_screen",
            "4200 cursor 179 159",
            "4300 cursor 180 160",
            *dw.press(4700, "button 1"),
            "6200 shot overwrite",
            "6400 cursor 253 292",
            "6500 cursor 254 293",
            *dw.press(6800, "button 1"),
            "15000 shot saved",
            "15400 quit",
        ]
    else:
        if case == "bow":
            lines += [
                *dw.hover(6800, 0),
                *dw.press(7400, "key E"),
                "9600 dump ram_bow",
                "9700 shot bow",
                *dw.press(10000, "key TAB"),
                "0 sync play 60",
                "800 button down 1",
                "1400 button up 1",
                "2100 button down 1",
                "2700 button up 1",
                "3400 button down 1",
                "4000 button up 1",
                "5100 dump ram_bow_shots",
                "5200 shot bow_shots",
                *dw.press(5500, "key TAB"),
                "0 sync screen 21 20",
                *dw.hover(800, 1),
                *dw.press(1600, "key E"),
                "3500 dump ram_return",
                "3600 shot return",
            ]
        else:
            lines += [*dw.hover(6800, 1), *dw.press(7400, "pad TRIANGLE"), "9600 dump ram_dropped", "9700 shot dropped"]
            if case == "drop-right":
                # Native removal compacts the dagger from cell 16 to cell 1.
                lines += [*dw.hover(10500, 1), *dw.press(11200, "key E"), "14000 dump ram_recovered"]
        lines += [
            *dw.press(4000 if case == "bow" else 17000, "key TAB"),
            "0 sync play 60",
            *attacks(),
            "5200 dump ram_after_attacks",
            "5300 shot after_attacks",
            "5600 quit",
        ]
    return "\n".join(lines) + "\n"


def dual_ready(s):
    right = next((w for w in s.weapons if w.handle == s.right), None)
    left = next((w for w in s.weapons if w.handle == s.shield_slot), None)
    return bool(
        right
        and left
        and right.handle != left.handle
        and right.one_handed
        and left.one_handed
        and right.equipped
        and left.equipped
        and not s.bow_drawn
        and s.on_left_bone == left.handle
        and s.left_owner == s.model_ref
        and s.model_ref
    )


def blade_flags(data, handle):
    r = dw.Ram(data)
    actor = r.u32(r.u32(handle + 0xFC) + 0x10)
    return r.u32(actor + 0x1C)


def bow_ammo(data, handle):
    r = dw.Ram(data)
    actor = r.u32(r.u32(handle + 0xFC) + 0x10)
    value = r.u32(actor + 0x14) ^ r.u32(0x4FE86C)
    return ((value >> 8) | (value << 24)) & 0xFFFFFFFF


def evaluate(case, out, source):
    raw = {p.stem: p.read_bytes() for p in out.glob("ram_*.ram")}
    states = {name: dw.read_state(data) for name, data in raw.items()}
    trace = "".join(p.read_text(encoding="latin-1") for p in sorted(out.glob("stdout_*.log")))
    checks = {}
    base = states.get("ram_04_left_equipped", dw.State())
    after = states.get("ram_after_attacks", dw.State())
    if case not in ("load", "reload"):
        checks["initial_dual"] = dual_ready(base)
    if case == "save":
        name = Path("BESCES-50006Slot1/Level 1A - Surdana.rsg")
        saved = out / "card/mc0" / name
        checks["save_written"] = (
            saved.is_file()
            and hashlib.sha256(saved.read_bytes()).digest() != hashlib.sha256((source / name).read_bytes()).digest()
        )
    elif case in ("load", "reload"):
        if case == "reload":
            changed = states.get("ram_changed", dw.State())
            checks["changed_before_reload"] = bool(
                changed.right and not changed.shield_slot and not changed.on_left_bone
            )
        checks["loaded_dual"] = dual_ready(states.get("ram_loaded", dw.State()))
        checks["after_attacks_dual"] = dual_ready(after)
    elif case == "bow":
        bow = states.get("ram_bow", dw.State())
        item = bow.weapon(0)
        checks["bow_owns_hand"] = bool(item and bow.bow_drawn and bow.on_left_bone == item.handle and item.equipped)
        checks["offhand_reserved"] = bool(base.shield_slot and bow.shield_slot == base.shield_slot)
        checks["offhand_not_render_attached"] = bool(
            base.shield_slot and "ram_bow" in raw and not (dw.Ram(raw["ram_bow"]).u32(base.shield_slot + 4) & 0x400000)
        )
        checks["offhand_hidden"] = bool(
            base.shield_slot and "ram_bow" in raw and not (dw.Ram(raw["ram_bow"]).u32(base.shield_slot + 4) & 0x3D)
        )
        checks["offhand_inactive"] = "ram_bow" in raw and not (blade_flags(raw["ram_bow"], base.shield_slot) & 8)
        shot = states.get("ram_bow_shots", dw.State())
        checks["bow_fired"] = bool(
            item
            and "ram_bow_shots" in raw
            and shot.bow_drawn
            and shot.on_left_bone == item.handle
            and 0 < bow_ammo(raw["ram_bow"], item.handle) - bow_ammo(raw["ram_bow_shots"], item.handle) <= 3
        )
        checks["offhand_inactive_after_shots"] = "ram_bow_shots" in raw and not (
            blade_flags(raw["ram_bow_shots"], base.shield_slot) & 8
        )
        checks["offhand_not_render_attached_after_shots"] = bool(
            base.shield_slot
            and "ram_bow_shots" in raw
            and not (dw.Ram(raw["ram_bow_shots"]).u32(base.shield_slot + 4) & 0x400000)
        )
        checks["return_restores_dual"] = dual_ready(states.get("ram_return", dw.State()))
        checks["offhand_hidden_after_shots"] = bool(
            base.shield_slot
            and "ram_bow_shots" in raw
            and not (dw.Ram(raw["ram_bow_shots"]).u32(base.shield_slot + 4) & 0x3D)
        )
        checks["return_restores_visibility"] = bool(
            base.shield_slot
            and "ram_return" in raw
            and dw.Ram(raw["ram_return"]).u32(base.shield_slot + 4) & 0x3D == 0x29
        )
        checks["return_restores_render_attachment"] = bool(
            base.shield_slot and "ram_return" in raw and dw.Ram(raw["ram_return"]).u32(base.shield_slot + 4) & 0x400000
        )
        checks["after_attacks_dual"] = dual_ready(after)
    else:
        dropped = states.get("ram_dropped", dw.State())
        club = base.weapon(1)
        checks["club_removed"] = bool(club and all(w.handle != club.handle for w in dropped.weapons))
        checks["left_cleared"] = dropped.shield_slot == 0 and dropped.on_left_bone == 0
        if case == "drop-left":
            checks["right_preserved"] = bool(base.right and dropped.right == base.right and after.right == base.right)
        else:
            checks["remaining_stowed"] = bool(dropped.right == 0 and all(not w.equipped for w in dropped.weapons))
            recovered = states.get("ram_recovered", dw.State())
            checks["remaining_usable"] = bool(
                recovered.right == base.shield_slot and after.right == base.shield_slot and recovered.shield_slot == 0
            )
        checks["dropped_blade_inactive"] = bool(
            club and "ram_after_attacks" in raw and not (blade_flags(raw["ram_after_attacks"], club.handle) & 8)
        )
    if case in ("load", "reload", "bow"):
        # Bow attacks deliberately have no attached off-hand. Score the dual-wield strikes only after returning
        # to melee, rather than rejecting the preceding bow shots for not playing a mirrored melee pose.
        combat_trace = trace.partition("label=ram_return")[2] if case == "bow" else trace
        combat = dw.analyze_trace(combat_trace)
        checks["alternating_strikes"] = combat["strikes"]["ok"]
        checks["correct_blade_windows"] = combat["blade"]["ok"]
    report = {
        "case": case,
        "checks": checks,
        "pass": all(checks.values()),
        "states": {name: asdict(s) for name, s in states.items()},
    }
    (out / "transition_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--save", type=Path, required=True)
    ap.add_argument("--name", default="dualwield-transitions")
    ap.add_argument(
        "--case", choices=("all", "bow", "drop-left", "drop-right", "save", "load", "reload"), default="all"
    )
    ap.add_argument("--timeout", type=int, default=180)
    a = ap.parse_args()
    source = a.save.resolve()
    cfg = dw.ROOT / "build/harness" / (a.name + "-config")
    cfg.mkdir(parents=True, exist_ok=True)
    exe = dw.ROOT / "dist/DrakanPC/drakan.exe"
    ini = cfg / "drakan.ini"
    ini.write_text(
        dw.patch_ini(
            (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
            {
                "video": dw.WINDOW,
                "general": {
                    "dual_wield": "1",
                    "inventory_screen": "native",
                    "pause_menu": "native",
                    "save_menu": "native",
                },
            },
        ),
        encoding="utf-8",
    )
    cases = ("bow", "drop-left", "drop-right", "save", "load", "reload") if a.case == "all" else (a.case,)
    reports = []
    saved_card = source
    for case in cases:
        card = saved_card if case in ("load", "reload") else source
        out = dw.run_game(a.name + "-" + case, card, recipe(case), ini, exe, a.timeout, cfg)
        report = evaluate(case, out, card)
        reports.append(report)
        print(json.dumps({"case": case, "checks": report["checks"], "pass": report["pass"]}), flush=True)
        if case == "save":
            if not report["pass"]:
                break
            saved_card = out / "card/mc0"
    (cfg / "report.json").write_text(json.dumps(reports, indent=2) + "\n", encoding="utf-8")
    return 0 if all(r["pass"] for r in reports) else 1


if __name__ == "__main__":
    raise SystemExit(main())
