"""Toggle dual wielding through the same live setter used by Settings; no UI or INI writes.

Requires the magic5 card saved with club right / dagger left (bow cell 0, club 1, dagger 16).
python scripts/harness/dual_wield_toggle.py --save <dual-wield-card>/mc0
"""

import argparse
import json
from pathlib import Path

import dual_wield_check as dw
from dual_wield_transitions import blade_flags, dual_ready


def recipe():
    return (
        "\n".join(
            [
                "clock milliseconds",
                "1 log on 60",
                "0 sync play 60",
                "500 dump ram_before",
                *dw.press(800, "button 1"),
                *dw.press(1400, "button 1"),
                "1700 dual_wield off",
                "3500 dump ram_disabled",
                "3600 shot disabled",
                "4000 dual_wield on",
                *dw.press(4400, "key TAB"),
                "0 sync screen 21 20",
                *dw.hover(800, 16),
                *dw.press(1600, "key E"),
                "3500 dump ram_rearmed",
                *dw.hover(4000, 0),
                *dw.press(4700, "key E"),
                "6500 dump ram_bow",
                *dw.press(6800, "key TAB"),
                "0 sync play 60",
                "800 dual_wield off",
                "2500 dump ram_disabled_bow",
                "2600 shot disabled_bow",
                "3000 dual_wield on",
                *dw.press(3400, "key TAB"),
                "0 sync screen 21 20",
                *dw.hover(800, 1),
                *dw.press(1600, "key E"),
                "3500 dump ram_return",
                "3800 quit",
            ]
        )
        + "\n"
    )


def evaluate(out):
    raw = {p.stem: p.read_bytes() for p in out.glob("ram_*.ram")}
    s = {k: dw.read_state(v) for k, v in raw.items()}
    base = s.get("ram_before", dw.State())
    disabled = s.get("ram_disabled", dw.State())
    rearmed = s.get("ram_rearmed", dw.State())
    bow = s.get("ram_bow", dw.State())
    disabled_bow = s.get("ram_disabled_bow", dw.State())
    returned = s.get("ram_return", dw.State())
    left = disabled.weapon(16)
    bow_left = disabled_bow.weapon(16)
    checks = {
        "initial_dual": dual_ready(base),
        "disable_stows_left": bool(
            base.right
            and disabled.right == base.right
            and not disabled.shield_slot
            and not disabled.on_left_bone
            and left
            and not left.equipped
        ),
        "blade_dead": "ram_disabled" in raw and not (blade_flags(raw["ram_disabled"], base.shield_slot) & 8),
        "reenable_can_equip": dual_ready(rearmed),
        "bow_drawn": bow.bow_drawn and bow.shield_slot == base.shield_slot,
        "bow_hides_offhand": bool(
            base.shield_slot and "ram_bow" in raw and not (dw.Ram(raw["ram_bow"]).u32(base.shield_slot + 4) & 0x40003D)
        ),
        "disable_preserves_bow": bool(
            bow.on_left_bone
            and disabled_bow.on_left_bone == bow.on_left_bone
            and disabled_bow.bow_drawn
            and not disabled_bow.shield_slot
            and bow_left
            and not bow_left.equipped
        ),
        "return_does_not_reequip": bool(
            returned.right == base.right and not returned.shield_slot and not returned.on_left_bone
        ),
        "disabled_bow_keeps_offhand_hidden": bool(
            base.shield_slot
            and "ram_disabled_bow" in raw
            and not (dw.Ram(raw["ram_disabled_bow"]).u32(base.shield_slot + 4) & 0x40003D)
        ),
    }
    report = {"pass": all(checks.values()), "checks": checks}
    (out / "toggle_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--save", type=Path, required=True)
    ap.add_argument("--name", default="dualwield-toggle")
    a = ap.parse_args()
    cfg = dw.ROOT / "build/harness" / (a.name + "-config")
    cfg.mkdir(parents=True, exist_ok=True)
    exe = dw.ROOT / "dist/DrakanPC/drakan.exe"
    ini = cfg / "drakan.ini"
    ini.write_text(
        dw.patch_ini(
            (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
            {"video": dw.WINDOW, "general": {"dual_wield": "1"}},
        ),
        encoding="utf-8",
    )
    out = dw.run_game(a.name, a.save.resolve(), recipe(), ini, exe, 180, cfg)
    report = evaluate(out)
    print(json.dumps(report, indent=2))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
