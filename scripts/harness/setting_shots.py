"""Capture the real pictures of the PC Settings help panel from the player's own game.

    python scripts/harness/setting_shots.py [--out dist/DrakanPC/previews/settings] [--only video.aspect,video.fov]

Starts the game in the first level, then for every visual setting applies each value through the settings overlay's
harness inbox (the real Apply path), closes the overlay, waits for the picture to settle and saves the presented frame
as previews/settings/<row key>/<value>.png (1280 x 720). The help panel (gameui/SettingPreview.h) shows the capture of
the value a row has right now. The pictures are screenshots of the disc's game: they stay on this machine (the play
folder keeps them across builds) and are never committed. Settings with no visible effect on a still picture (window
mode, VSync, volume, mouse speed ...) have no capture on purpose.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from dual_wield_check import patch_ini
from play_run import ROOT
from settings_check import Driver, state, wait_for

# (row key, values, setup applied first as (key, value) pairs, settle seconds). Choice values are the schema's values;
# slider values are sampled (the panel shows the nearest capture).
PLAN = [
    ("video.aspect", ["4:3", "16:9", "21:9", "stretch"], [], 3),
    ("video.fov", ["60", "80", "100", "120"], [("video.aspect", "16:9")], 3),
    ("video.hud_scale", ["0.5", "1", "1.5"], [("video.aspect", "16:9")], 3),
    ("video.hud_aspect_correct", ["off", "on"], [("video.aspect", "21:9")], 3),
    ("video.scale", ["1", "2", "4"], [("video.aspect", "16:9")], 5),
    ("video.filter", ["game", "point", "bilinear"], [], 3),
    ("video.anisotropic", ["1", "2", "4", "8", "16"], [("video.filter", "bilinear")], 3),
    ("video.hd_textures", ["off", "on"], [], 8),
    ("video.modern_water", ["off", "on"], [], 8),
    ("video.ray_shadows", ["off", "on"], [], 12),
    ("video.ray_reflections", ["off", "on"], [], 12),
    ("video.ray_gi", ["off", "on"], [], 12),
]
BASELINE = {
    "video.aspect": "16:9",
    "video.fov": "80",
    "video.hud_scale": "0",
    "video.hud_aspect_correct": "on",
    "video.scale": "auto",
    "video.filter": "game",
    "video.anisotropic": "1",
    "video.hd_textures": "on",
    "video.modern_water": "off",
    "video.ray_shadows": "off",
    "video.ray_reflections": "off",
    "video.ray_gi": "off",
}


def settled(path: Path) -> None:
    """The game writes the bitmap while the file already exists: wait until its size stops changing."""
    last = -1
    for _ in range(100):
        size = path.stat().st_size
        if size == last and size > 0:
            return
        last = size
        time.sleep(0.4)


def file_value(value: str) -> str:
    return value.replace(":", "x")


def toggle_value(key: str, value: str) -> str:
    return {"off": "0", "on": "1"}.get(value, value)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", default=str(ROOT / "dist/DrakanPC/drakan.exe"))
    ap.add_argument("--out", default=str(ROOT / "dist/DrakanPC/previews/settings"))
    ap.add_argument("--only", default="", help="comma separated row keys")
    ap.add_argument("--level", default="Level 1A - Surdana")
    ap.add_argument("--force", action="store_true", help="capture again even when the picture exists")
    ap.add_argument("--tries", type=int, default=6, help="game starts (the game is restarted if it dies or hangs)")
    args = ap.parse_args()
    from PIL import Image

    exe = Path(args.exe)
    run = ROOT / "build/harness/setting-shots"
    if run.exists():
        shutil.rmtree(run)
    (run / "card/mc0").mkdir(parents=True)
    ini = patch_ini(
        (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
        {
            "general": {
                "title_menu": "native",
                "pause_menu": "native",
                "pause_when_unfocused": "0",
                "resume_on_close": "1",  # closing the overlay resumes play: no pause menu in the picture
            },
            "video": {"window_width": "1920", "window_height": "1080", "aspect": "16:9"},
        },
    )
    (run / "drakan.ini").write_text(ini, encoding="utf-8")
    shutil.copy(exe.parent / "drakan_input.ini", run / "drakan_input.ini")
    (run / "recipe.txt").write_text("clock milliseconds\n0 sync play 80\n3000000 quit\n")
    env = dict(os.environ)
    env.update(
        DRAKAN_NO_ACTIVATE="1",
        DRAKAN_AUDIO="0",
        DRAKAN_CONSOLE="0",
        PS2X_GS="d3d11",
        DRAKAN_INPUT_SCRIPT=str(run / "recipe.txt"),
        DRAKAN_INI=str(run / "drakan.ini"),
        DRAKAN_INPUT_INI=str(run / "drakan_input.ini"),
        DRAKAN_SHOT_DIR=str(run),
        DRAKAN_LOG_DIR=str(run),
        DRAKAN_OVERLAY_SHOT_DIR=str(run),
        DRAKAN_MEMCARD_DIR=str(run / "card/mc0"),
        DRAKAN_NATIVE_OPTIONS_INI=str(run / "native.ini"),
        DRAKAN_INPUT_AUTOSTART="1",
        DRAKAN_AUTOPRESS="2000",
        DRAKAN_START_LEVEL=args.level,
        DRAKAN_FAST_START="1",
        DRAKAN_PAUSE_UNFOCUSED="0",
        DRAKAN_RESUME_ON_CLOSE="1",
        PS2X_MAX_SECONDS="1800",
    )
    only = {k for k in args.only.split(",") if k}
    out_root = Path(args.out)
    driver = Driver(run)
    saved = 0
    for attempt in range(args.tries):
        with (run / "stdout.log").open("wb") as log:
            game = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                saved += capture(game, run, driver, only, out_root, Image, args.force)
                break
            except (TimeoutError, RuntimeError) as e:
                print(f"attempt {attempt + 1}: {e}; restarting the game for the pictures still missing")
            finally:
                game.kill()
                game.wait()
        (run / "command.txt").unlink(missing_ok=True)
        (run / "ack.txt").unlink(missing_ok=True)
        driver.sequence = 0
    print(f"{saved} pictures in {out_root}")
    return 0


def capture(game, run, driver, only, out_root, Image, args_force=False) -> int:
    saved = 0

    def apply(key: str, value: str, settle: float) -> bool:
        if game.poll() is not None:
            raise RuntimeError(f"the game exited (code {game.returncode}) before {key}={value}")
        ok = True
        try:
            driver.command("toggle")
            time.sleep(0.5)
            driver.command(f"apply {key} {toggle_value(key, value)}")
        except TimeoutError:
            ok = False
        try:
            driver.command("close")
        except TimeoutError:
            ok = False
        time.sleep(settle)
        return ok

    if True:
        wait_for(lambda: state(run).get("play"), 240)
        time.sleep(8)  # let the level settle: idle pose, camera behind Rynn
        for key, values, setup, settle in PLAN:
            if only and key not in only:
                continue
            todo = [v for v in values if args_force or not (out_root / key / f"{file_value(v)}.png").exists()]
            if not todo:
                continue
            for k, v in setup:
                apply(k, v, 2)
            for value in todo:
                if not apply(key, value, settle):
                    print(f"skip {key}={value}: not applied")
                    continue
                name = f"cap_{key}_{file_value(value)}"
                for attempt in range(4):  # a request can be missed while the game resumes: ask again
                    try:
                        driver.shot(name)
                        break
                    except TimeoutError:
                        if attempt == 3 or game.poll() is not None:
                            raise
                        time.sleep(2)
                dest = out_root / key / f"{file_value(value)}.png"
                dest.parent.mkdir(parents=True, exist_ok=True)
                settled(run / f"{name}.bmp")
                img = Image.open(run / f"{name}.bmp").convert("RGB").resize((1280, 720), Image.LANCZOS)
                img.save(dest, optimize=True)
                (run / f"{name}.bmp").unlink()
                saved += 1
                print(f"saved {dest}")
            for k, v in setup:
                apply(k, BASELINE.get(k, v), 1)
            apply(key, BASELINE[key], 2)
    return saved


if __name__ == "__main__":
    sys.exit(main())
