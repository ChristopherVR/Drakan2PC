"""Check settings through play_run and the render-thread harness inbox, without desktop input."""

from __future__ import annotations

import argparse
import json
import math
import os
import re
import shutil
import struct
import time
from concurrent.futures import ThreadPoolExecutor

from dual_wield_check import patch_ini
from play_run import ROOT, run_once


def wait_for(predicate, timeout=60):
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if value := predicate():
            return value
        time.sleep(0.05)
    raise TimeoutError("Harness condition did not become true")


def state(out):
    try:
        text = (out / "state.txt").read_text(encoding="utf-8")
    except OSError:
        return {}
    match = re.search(
        r"open=(\d) screen (-?\d+), camera (\d), scripted (\d), title seen (\d) samples=(\d+) hold=(\d)", text
    )
    keys = ("open", "screen", "play", "scripted", "title", "samples", "hold")
    return dict(zip(keys, map(int, match.groups()))) if match else {}


def camera_state(out, name, profile):
    data = (out / f"{name}.ram").read_bytes()

    def u32(a):
        return struct.unpack_from("<I", data, a & 0x1FFFFFFF)[0]

    def f32(a):
        return struct.unpack_from("<f", data, a & 0x1FFFFFFF)[0]

    address = 0x5565D4 if profile["identity_addresses"] else int(profile["addresses"]["0x005565d4"], 0)
    l = {key: int(value, 0) if isinstance(value, str) else value for key, value in profile["camera"].items()}
    ctx = u32(address)

    def object_at(offset):
        return u32(u32(u32(ctx + offset) + l["handle_inner"]) + l["inner_data"])

    cam = object_at(l["context_camera"])
    player = object_at(l["context_actor"])
    transform = u32(player + l["player_transform"])
    return {
        "pitch": f32(cam + l["camera_pitch"]),
        "yaw": math.atan2(f32(transform + l["transform_back"]), f32(transform + l["transform_right"])),
        "position": [f32(transform + l["transform_position"] + i * 4) for i in range(3)],
    }


def angle_delta(a, b):
    return math.remainder(b - a, 2 * math.pi)


class Driver:
    def __init__(self, out):
        self.out = out
        self.sequence = 0

    def command(self, text):
        self.sequence += 1
        tmp = self.out / "command.tmp"
        tmp.write_text(f"{self.sequence} {text}")
        tmp.replace(self.out / "command.txt")

        def acknowledged():
            try:
                return (self.out / "ack.txt").read_text(encoding="utf-8") == f"{self.sequence} ok"
            except OSError:
                return False

        wait_for(acknowledged, 15)

    def expect(self, **values):
        def matches():
            current = state(self.out)
            return current if all(current.get(k) == v for k, v in values.items()) else None

        return wait_for(matches, 90)

    def shot(self, name):
        (self.out / "shot.txt").write_text(name)
        wait_for((self.out / f"{name}.bmp").exists, 15)


def run(region, mode, resume, *, level=None, name=None, timeout=300):
    if mode == "camera":
        resume = 1
    profile = json.loads((ROOT / f"configs/regions/{region}.json").read_text(encoding="utf-8"))
    title_screen = profile["input_flow"]["title_screen"]
    title_screen = int(title_screen, 0) if isinstance(title_screen, str) else title_screen
    pause_screen = profile["input_flow"]["pause_screen"]
    pause_screen = int(pause_screen, 0) if isinstance(pause_screen, str) else pause_screen
    exe = ROOT / "dist" / ("DrakanPC" if region == "pal" else f"DrakanPC-{region}") / "drakan.exe"
    out = ROOT / "build/harness" / (name or f"{region}-settings-{mode}-resume{resume}")
    if out.resolve().parent != (ROOT / "build/harness").resolve():
        raise ValueError("Run directory must stay within build/harness")
    if out.exists():
        shutil.rmtree(out)
    (out / "card/mc0").mkdir(parents=True)
    ini = patch_ini(
        (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
        {
            "general": {
                "resume_on_close": str(resume),
                "pause_when_unfocused": "0",
                "title_menu": "native",
                "pause_menu": "native",
            }
        },
    )
    (out / "drakan.ini").write_text(ini, encoding="utf-8")
    input_ini = (exe.parent / "drakan_input.ini").read_text(encoding="utf-8")
    if mode == "camera":
        input_ini = patch_ini(
            input_ini,
            {
                "general": {"look_mode": "direct"},
                "mouse": {
                    "sensitivity_x": "0.002",
                    "sensitivity_y": "0.002",
                    "invert_x": "0",
                    "invert_y": "0",
                    "pitch_persist": "1",
                    "pitch_view_gain": "1.8",
                    "free_combat": "1",
                    "camera_follow": "5",
                },
            },
        )
    (out / "drakan_input.ini").write_text(input_ini, encoding="utf-8")
    if mode == "title":
        recipe = f"clock milliseconds\n0 sync screen {title_screen} 3\n0 dump ram_title\n30000 quit\n"
    elif mode == "camera":
        recipe = (
            "0 sync play 80\n0 dump ram_start\n1 mouse 64 0\n2 dump ram_yaw\n"
            "15 dump ram_yaw_idle\n16 mouse 0 -36\n17 dump ram_pitch\n30 dump ram_pitch_idle\n"
            f"31 sync screen {pause_screen} 3\n31 dump ram_settings\n32 sync play 5\n32 dump ram_reload_base\n"
            "33 mouse 64 0\n34 dump ram_reload\n35 quit\n"
        )
    else:
        recipe = (
            "0 sync play 80\n0 dump ram_ready\n"
            f"1 sync screen {pause_screen} 3\n1 dump ram_open\n"
            "2 key down W\n2 mouse 10 -5 20\n22 dump ram_paused\n23 key up W\n"
            "24 sync play 5\n24 dump ram_after\n"
        )
        if resume:
            recipe += f"25 sync screen {pause_screen} 3\n25 dump ram_existing_pause\n26 sync play 5\n26 quit\n"
        else:
            recipe += "25 quit\n"
    (out / "recipe.txt").write_text(recipe)
    env = dict(os.environ)
    env.update(
        DRAKAN_NO_ACTIVATE="1",
        DRAKAN_AUDIO="0",
        DRAKAN_CONSOLE="0",
        PS2X_GS="d3d11",
        DRAKAN_INPUT_SCRIPT=str(out / "recipe.txt"),
        DRAKAN_INI=str(out / "drakan.ini"),
        DRAKAN_INPUT_INI=str(out / "drakan_input.ini"),
        DRAKAN_SHOT_DIR=str(out),
        DRAKAN_LOG_DIR=str(out),
        DRAKAN_OVERLAY_SHOT_DIR=str(out),
        DRAKAN_MEMCARD_DIR=str(out / "card/mc0"),
        DRAKAN_NATIVE_OPTIONS_INI=str(out / "native.ini"),
        DRAKAN_AUTOPRESS="2000",
        DRAKAN_AUTOPRESS_UNTIL="8000" if mode == "title" else "100000",
        DRAKAN_INPUT_AUTOSTART="0" if mode == "title" else "1",
        DRAKAN_PAUSE_UNFOCUSED="0",
        DRAKAN_RESUME_ON_CLOSE=str(resume),
        PS2X_MAX_SECONDS=str(timeout),
    )
    if level:
        env["DRAKAN_START_LEVEL"] = level
    checks = {}
    driver = Driver(out)
    with ThreadPoolExecutor(max_workers=1) as pool:
        result = pool.submit(run_once, exe, env, out, timeout + 20, 1)
        try:
            if mode == "title":
                initial = wait_for(lambda: state(out), 30)
                if initial["title"] == 0:
                    driver.command("toggle")
                    checks["startup_hotkey_blocked"] = state(out).get("open") == 0
                else:
                    raise RuntimeError("Missed startup gate observation")
                driver.expect(open=0, screen=title_screen)
                driver.command("toggle")
                driver.expect(open=1, screen=title_screen, hold=0)
                driver.shot("title_settings")
                driver.command("controls")
                driver.command("apply input.mouse.sensitivity_x 0.003")
                driver.shot("controls")
                driver.command("close")
                driver.expect(open=0, screen=title_screen)
                checks["title_hotkey_apply_close"] = True
                driver.command("pad DPAD_DOWN")
                time.sleep(0.3)
                driver.command("pad CROSS")
                driver.expect(open=0, screen=9)
                driver.command("pad DPAD_DOWN")
                time.sleep(0.3)
                driver.command("pad DPAD_DOWN")
                time.sleep(0.3)
                driver.command("pad CROSS")
                driver.expect(open=1, screen=9)
                driver.shot("native_options_settings")
                driver.command("close")
                driver.expect(open=0, screen=9)
                checks["native_options_entry"] = True
            elif mode == "camera":
                wait_for((out / "ram_pitch_idle.ram").exists, timeout - 40)
                driver.command("toggle")
                driver.expect(open=1, screen=pause_screen, hold=0)
                wait_for((out / "ram_settings.ram").exists, 15)
                driver.command("controls")
                driver.command("apply input.mouse.sensitivity_x 0.004")
                driver.shot("direct_controls")
                driver.command("close")
                driver.expect(open=0, screen=-1)
                checks["clean_script_exit"] = result.result(timeout=timeout + 20)
                samples = {
                    name: camera_state(out, f"ram_{name}", profile)
                    for name in ("start", "yaw", "yaw_idle", "pitch", "pitch_idle", "reload_base", "reload")
                }
                (out / "camera.json").write_text(json.dumps(samples, indent=2) + "\n")
                checks["raw_yaw"] = abs(angle_delta(samples["start"]["yaw"], samples["yaw"]["yaw"]) + 0.128) < 0.001
                checks["no_yaw_drift"] = abs(angle_delta(samples["yaw"]["yaw"], samples["yaw_idle"]["yaw"])) < 0.001
                checks["raw_pitch"] = abs(samples["pitch"]["pitch"] - samples["yaw_idle"]["pitch"] - 0.04) < 0.001
                checks["persistent_pitch"] = abs(samples["pitch_idle"]["pitch"] - samples["pitch"]["pitch"]) < 0.001
                checks["live_sensitivity_reload"] = (
                    abs(angle_delta(samples["reload_base"]["yaw"], samples["reload"]["yaw"]) + 0.256) < 0.001
                )
            else:
                wait_for((out / "ram_ready.ram").exists, timeout - 40)
                driver.command("toggle")
                before = driver.expect(open=1, screen=pause_screen, hold=0)
                driver.shot("paused_settings")
                wait_for((out / "ram_paused.ram").exists, 15)
                checks["native_pause_and_live_menu"] = state(out).get("samples", 0) > before["samples"]
                driver.command("close")
                driver.expect(open=0, screen=-1 if resume else pause_screen)
                checks["close_policy"] = True
                opened = camera_state(out, "ram_open", profile)
                paused = camera_state(out, "ram_paused", profile)
                checks["overlay_blocks_motion"] = opened == paused
                if resume:
                    wait_for((out / "ram_after.ram").exists, 15)
                    driver.command("pad START")
                    driver.expect(open=0, screen=pause_screen)
                    wait_for((out / "ram_existing_pause.ram").exists, 15)
                driver.command("toggle")
                driver.expect(open=1, screen=pause_screen)
                driver.command("close")
                driver.expect(open=0, screen=pause_screen)
                checks["preexisting_pause_preserved"] = True
                # Explicitly resume only after verifying that closing preserved the pause.
                # The final script barrier then permits a clean quit.
                driver.command("pad START")
            checks["clean_script_exit"] = result.result(timeout=timeout + 20)
            checks["settings_saved"] = mode != "title" or "sensitivity_x = 0.003" in (
                out / "drakan_input.ini"
            ).read_text(encoding="utf-8")
        except (TimeoutError, RuntimeError) as error:
            checks["error"] = str(error)
            result.result(timeout=timeout + 30)
    report = {
        "region": region,
        "mode": mode,
        "level": level,
        "resume": resume,
        "pass": all(v is True for v in checks.values()),
        "checks": checks,
    }
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps(report, indent=2), flush=True)
    return report["pass"]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--region", default="usa")
    parser.add_argument("--mode", choices=["title", "gameplay", "camera"], default="title")
    parser.add_argument("--resume", type=int, choices=[0, 1], default=0)
    parser.add_argument("--level")
    parser.add_argument("--name")
    parser.add_argument("--timeout", type=int, default=300)
    args = parser.parse_args()
    return 0 if run(args.region, args.mode, args.resume, level=args.level, name=args.name, timeout=args.timeout) else 1


if __name__ == "__main__":
    raise SystemExit(main())
