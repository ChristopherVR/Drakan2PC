"""Strafe both directions while dual wielding; requires a card saved with two weapons drawn.

python scripts/harness/dual_wield_movement.py --save <dual-wield-card>/mc0
"""

import argparse
import json
import re
from pathlib import Path

import dual_wield_check as dw


def recipe():
    events = []
    for t, key in [(800, "A"), (8000, "D")]:
        events += [f"{t} key down {key}", f"{t + 6000} key up {key}"]
        for i in range(9):
            events += dw.press(t + 300 + i * 600, "button 1")
        events += [f"{t + 1300 + i * 900} shot {key}_{i}" for i in range(5)]
    return (
        "\n".join(
            [
                "clock milliseconds",
                "1 log on 30",
                "0 sync play 60",
                *sorted(events, key=lambda s: int(s.split()[0])),
                "15000 quit",
            ]
        )
        + "\n"
    )


def evaluate(text):
    errors = [float(s) for s in re.findall(r"\[dual-wield\] locomotion split=1 lowerError=([\d.]+)", text)]
    roots = [tuple(map(float, m.groups())) for m in dw.ROOT_MOTION.finditer(text)]
    moving = [v for v in roots if v[0] == 0]
    combat = dw.analyze_trace(text)
    checks = {
        "native_legs": bool(errors) and max(errors) < 1e-4,
        "native_movement": bool(moving) and all(v[1:4] == v[4:7] for v in moving),
        "alternating_attacks": combat["strikes"]["ok"],
        "blade_windows": combat["blade"]["ok"],
    }
    return {
        "pass": all(checks.values()),
        "checks": checks,
        "moving_poses": len(errors),
        "worst_leg_error": max(errors, default=None),
        "native_root_moves": len(moving),
        "strikes": combat["strikes"],
    }


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--save", type=Path, required=True)
    ap.add_argument("--name", default="dualwield-movement")
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
    out = dw.run_game(a.name, a.save.resolve(), recipe(), ini, exe, 120, cfg)
    report = evaluate("".join(p.read_text(encoding="latin-1") for p in sorted(out.glob("stdout_*.log"))))
    (out / "movement_report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if report["pass"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
