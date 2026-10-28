"""Read-only engine outcome checks for combat_movement_followup.txt; no launch.
Run inventory checker separately for the seven native Equipment assertions.
These checks deliberately fail for missing/unloaded snapshots or no shot outcome.
"""

import argparse
import json
import math
import pathlib
import subprocess
import sys

p = argparse.ArgumentParser()
p.add_argument("directory", type=pathlib.Path)
p.add_argument("--sens-x", type=float, default=0.002)
p.add_argument("--sens-y", type=float, default=0.002)
p.add_argument("--invert-x", action="store_true")
p.add_argument("--invert-y", action="store_true")
p.add_argument("--camera-only", action="store_true")
a = p.parse_args()
raw = json.loads(
    subprocess.check_output(
        [sys.executable, str(pathlib.Path(__file__).with_name("combat_ram_report.py")), str(a.directory)], text=True
    )
)
snaps = {s["ram"]: s for s in raw}
checks = []


def check(name, passed, **evidence):
    checks.append(dict(name=name, passed=bool(passed), **evidence))


def finish():
    print(json.dumps({"checks": checks, "snapshots": raw}, indent=2, allow_nan=False))
    raise SystemExit(0 if checks and all(c["passed"] for c in checks) else 1)


def get(name):
    s = snaps.get("ram_" + name + ".ram", {})
    if not s.get("rynn_available"):
        raise ValueError(f"{name}: {s.get('reason', 'snapshot missing')}")
    return s


try:
    base, moved, returned = [get("look_" + s) for s in ("base", "moved", "return")]
    pitch = moved["camera_pitch_rad"] - base["camera_pitch_rad"]
    expected = 20 * a.sens_y * (-1 if a.invert_y else 1)
    check(
        "native pitch responds to raw mouse distance and configured inversion",
        abs(pitch - expected) < 0.01,
        observed=pitch,
        expected=expected,
    )
    yaw = math.remainder(moved["heading_basis_xz_rad"] - base["heading_basis_xz_rad"], 2 * math.pi)
    expected_yaw = -40 * a.sens_x * (-1 if a.invert_x else 1)
    check(
        "native heading responds to configured mouse distance and inversion",
        abs(yaw - expected_yaw) < 0.02,
        observed=yaw,
        expected=expected_yaw,
    )
    back = math.remainder(returned["heading_basis_xz_rad"] - base["heading_basis_xz_rad"], 2 * math.pi)
    check(
        "opposite mouse motion returns heading and pitch",
        abs(back) < 0.02 and abs(returned["camera_pitch_rad"] - base["camera_pitch_rad"]) < 0.01,
        heading_residual=back,
        pitch_residual=returned["camera_pitch_rad"] - base["camera_pitch_rad"],
    )
    if a.camera_only:
        finish()
    forward, strafe = [get(s) for s in ("forward", "strafe")]
    fv = [forward["position"][i] - returned["position"][i] for i in (0, 2)]
    sv = [strafe["position"][i] - forward["position"][i] for i in (0, 2)]
    fm, sm = math.hypot(*fv), math.hypot(*sv)
    check("forward key produces native ground displacement", fm > 1, displacement_xz=fv)
    check(
        "strafe key produces distinct lateral native displacement",
        sm > 1 and fm > 1 and abs(sum(x * y for x, y in zip(fv, sv)) / (fm * sm)) < 0.6,
        displacement_xz=sv,
    )
    first, last = get("look_base"), get("shots_settled")
    shotnames = ["look_base", "shot1_active", "shot1_release", "shot2_release", "shot3_release", "shots_settled"]
    quantities = {name: get(name).get("selected_item", {}).get("quantity_candidate") for name in shotnames}
    equipped = first["equipped_channels"]["EAC"]
    check("native equipped weapon is bow resource1004f", equipped["resource_class"] == 0x1004F, equipped=equipped)
    before, after = quantities["look_base"], quantities["shots_settled"]
    check(
        "bow attack consumes actual equipped ammo",
        before is not None and after is not None and 0 < before - after <= 3,
        ammo_by_snapshot=quantities,
    )
    check(
        "bow release snapshot has no stale attack state",
        last["attack_state1040"] == 0,
        attack_state1040=last["attack_state1040"],
    )
    quick = get("quick_released")
    check(
        "Quick Inventory closes with native flow inactive",
        quick["inventory"]["global_active"] == 0 and quick["inventory"]["controller0c"] == 0,
        inventory=quick["inventory"],
        equipment=quick["equipped_channels"],
    )
except (KeyError, TypeError, ValueError) as error:
    check("required engine observations available", False, error=str(error))
finish()
