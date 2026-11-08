"""Make the real pictures of the PC Settings help panel from the player's own game.

    python scripts/harness/setting_shots.py                      # every picture that is missing
    python scripts/harness/setting_shots.py --only video.fov     # one setting
    python scripts/harness/setting_shots.py --force              # all again

Every picture is its own short game run: the first level, the setting written into a private drakan.ini, Rynn placed at a
fixed spot and the window saved on a fixed input sample (`place` unless it is the start spot, then `shot` N samples later). The game's logic runs on
samples, so every run reaches the same frame of the same scene and the only difference between two pictures of a setting
is the setting itself. Scenes: the meadow where Rynn starts (field of view, aspect, HUD, filtering, shadows, vegetation)
and the pond (water, reflections). The picture is saved as previews/settings/<row key>/<value>.png (1280 x 720 window) next to
the executable; the panel (gameui/SettingPreview.h) shows the one for the value a row has right now. Nothing is committed:
the pictures are made from the player's disc by the build (Build-LocalGame.ps1 -SettingPictures) and kept
across builds. Settings with no visible effect on a still picture have none on purpose, and neither do values like
Auto aspect that depend on the window.
"""

from __future__ import annotations

import argparse
import os
import shutil
import subprocess
import sys
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dual_wield_check import patch_ini
from map_previews import read_png, write_png
from play_run import ROOT

MEADOW = None  # where Rynn starts: trees, grass, a tent at the edge (no placement)
POND = ("382176", "163000", "522638", "1.571")  # the Surdana pond, looking across the water
WALK = 0  # samples Rynn walks forward before the picture (0 = stand)
WIDTH, HEIGHT = 1280, 720  # the window is the picture: no resizing, standard library only
BASE = {"aspect": "16:9", "hud_scale": "1", "fov": "80", "scale": "1", "filter": "game", "anisotropic": "1"}
RAYS = ("ray_shadows", "ray_reflections", "ray_gi", "modern_water")

# (setting, values, scene, extra ini for the scene)
PLAN = [
    ("aspect", ["4:3", "16:9", "21:9", "stretch"], MEADOW, {}),
    ("fov", [str(d) for d in range(60, 121)], MEADOW, {}),
    ("hud_scale", ["auto", "0.5", "0.6", "0.7", "0.8", "0.9", "1", "1.25", "1.5"], MEADOW, {}),
    ("hud_aspect_correct", ["off", "on"], MEADOW, {"aspect": "21:9"}),
    ("scale", ["auto", "1", "2", "3", "4"], MEADOW, {}),
    ("filter", ["game", "point", "bilinear"], MEADOW, {}),
    ("anisotropic", ["1", "2", "4", "8", "16"], MEADOW, {"filter": "bilinear"}),
    ("hd_textures", ["off", "on"], MEADOW, {}),
    ("modern_water", ["off", "on"], POND, {}),
    ("ray_reflections", ["off", "on"], POND, {}),
    ("ray_shadows", ["off", "on"], MEADOW, {}),
    ("ray_gi", ["off", "on"], MEADOW, {}),
    ("foliage_wind", ["off", "on"], MEADOW, {}),
    ("foliage_wind_strength", ["0.25", "0.5", "1", "1.5", "2"], MEADOW, {"foliage_wind": "1"}),
]


def file_value(value: str) -> str:
    return value.replace(":", "x")


def ini_value(key: str, value: str) -> str:
    if key in ("scale", "hud_scale"):
        return value
    return {"off": "0", "on": "1"}.get(value, value)


def walk(wait: int) -> str:
    """Standing still Rynn plays random idle gestures (different in every run); a walk cycle is the same in all of them."""
    return f"{20 + wait - WALK} key down W\n" if WALK else ""


def run_one(exe: Path, key: str, value: str, scene, extra: dict, wait: int, workdir: Path) -> Path | None:
    """One game run; returns the window PNG or None. `wait` = samples between the placement and the picture."""
    run = workdir / f"{key}_{file_value(value)}"
    shutil.rmtree(run, ignore_errors=True)
    (run / "card/mc0").mkdir(parents=True)
    values = dict(BASE)
    values.update(extra)
    values[key] = ini_value(key, value)
    values.update({"window_width": str(WIDTH), "window_height": str(HEIGHT), "show_fps": "0"})
    ini = patch_ini(
        (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
        {
            "general": {"title_menu": "native", "pause_menu": "native", "pause_when_unfocused": "0"},
            "video": values,
        },
    )
    (run / "drakan.ini").write_text(ini, encoding="utf-8")
    shutil.copy(exe.parent / "drakan_input.ini", run / "drakan_input.ini")
    place = f"20 place {' '.join(scene)}\n" if scene else ""
    # Three pictures 25 samples apart: Rynn must stand still in all of them (see `change`), the last one is kept.
    shots = "".join(f"{20 + wait + 25 * i} shot picture{i}\n" for i in range(3))
    (run / "recipe.txt").write_text(f"0 sync play 80\n{place}{walk(wait)}{shots}{120 + wait} quit\n")
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
        DRAKAN_MEMCARD_DIR=str(run / "card/mc0"),
        DRAKAN_NATIVE_OPTIONS_INI=str(run / "native.ini"),
        DRAKAN_INPUT_AUTOSTART="1",
        DRAKAN_AUTOPRESS="2000",
        DRAKAN_START_LEVEL="Level 1A - Surdana",
        DRAKAN_FAST_START="1",
        DRAKAN_PAUSE_UNFOCUSED="0",
        PS2X_MAX_SECONDS="300",
    )
    for name in ("DRAKAN_WS_ASPECT", "DRAKAN_HUD_SCALE", "DRAKAN_TITLE_MENU", "DRAKAN_PAUSE_MENU"):
        env.pop(name, None)
    with (run / "stdout.log").open("wb") as log:
        proc = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            proc.wait(timeout=320)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            return None
    shots = [run / f"picture{i}.png" for i in range(3)]
    text = (run / "stdout.log").read_bytes().decode("latin-1")
    if not all(f.is_file() for f in shots) or "script quit" not in text:
        return None
    if key in RAYS and value == "on":
        # The ray pass builds in the background; a picture taken before it is ready shows no effect at all.
        ready, taken = text.find("ray tracing ready"), text.find("picture2.png")
        if ready < 0 or taken < ready:
            return None
    return shots


def change(a: bytes, b: bytes) -> float:
    """Mean absolute difference of two window pictures over the middle of the frame, where Rynn stands."""
    w, h, bpp, pa = read_png(a)
    pb = read_png(b)[3]
    total = n = 0
    for y in range(int(h * 0.2), int(h * 0.95), 3):
        for x in range(int(w * 0.3), int(w * 0.7), 3):
            i = (y * w + x) * bpp
            total += abs(pa[i] - pb[i]) + abs(pa[i + 1] - pb[i + 1]) + abs(pa[i + 2] - pb[i + 2])
            n += 3
    return total / n


STILL = 0.6  # a standing Rynn changes less than this between two pictures 25 samples apart; a gesture changes more


def downscale(png: bytes, path: Path) -> None:
    """Writes `png` as a WIDTH x HEIGHT picture (the game saves the internal resolution: 1x = the window, 4x = four times)."""
    w, h, bpp, px = read_png(png)
    k = w // WIDTH
    if k <= 1:
        path.write_bytes(png)
        return
    out = bytearray()
    area = k * k
    for y in range(h // k):
        rows = [px[(y * k + j) * w * bpp : (y * k + j + 1) * w * bpp] for j in range(k)]
        for x in range(w // k):
            for c in range(3):
                out.append(sum(r[(x * k + i) * bpp + c] for r in rows for i in range(k)) // area)
    write_png(path, w // k, h // k, bytes(out))


def message_shown(png: bytes) -> bool:
    """The quest message of the level start (white text and a book icon, top left) is fading out on wall-clock time."""
    w, h, bpp, px = read_png(png)
    count = 0
    for y in range(int(h * 0.015), int(h * 0.09)):
        for x in range(int(w * 0.12), int(w * 0.69)):
            i = (y * w + x) * bpp
            count += min(px[i], px[i + 1], px[i + 2]) > 205
    return count > 20


def make(exe: Path, out_root: Path, workdir: Path, job) -> str:
    key, value, scene, extra = job
    dest = out_root / f"video.{key}" / f"{file_value(value)}.png"
    for attempt in range(6):
        wait = 360 + attempt * (500 if key in RAYS else 120)
        shots = run_one(exe, key, value, scene, extra, wait, workdir)
        if shots:
            data = [f.read_bytes() for f in shots]
            # No quest message (it fades on wall-clock time) and Rynn standing still (she plays random idle gestures).
            ok = not message_shown(data[2]) and max(change(data[0], data[1]), change(data[1], data[2])) < STILL
            if ok:
                dest.parent.mkdir(parents=True, exist_ok=True)
                downscale(data[2], dest)
            shutil.rmtree(shots[0].parent, ignore_errors=True)
            if ok:
                return f"saved video.{key}={value}"
        time.sleep(2)
    return f"FAILED video.{key}={value}"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "dist/DrakanPC/drakan.exe"))
    ap.add_argument("--out", default=str(ROOT / "dist/DrakanPC/previews/settings"))
    ap.add_argument("--only", default="", help="comma separated settings (fov, aspect, ray_gi ...)")
    ap.add_argument("--force", action="store_true", help="make the picture again even when it exists")
    ap.add_argument("--jobs", type=int, default=3, help="game runs at the same time")
    args = ap.parse_args()
    exe, out_root = Path(args.exe), Path(args.out)
    only = {k.removeprefix("video.") for k in args.only.split(",") if k}
    workdir = ROOT / "build/harness/setting-shots"
    workdir.mkdir(parents=True, exist_ok=True)
    jobs = []
    for key, values, scene, extra in PLAN:
        if only and key not in only:
            continue
        for value in values:
            if args.force or not (out_root / f"video.{key}" / f"{file_value(value)}.png").exists():
                jobs.append((key, value, scene, extra))
    print(f"{len(jobs)} pictures to make", flush=True)
    failed = 0
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for message in pool.map(lambda j: make(exe, out_root, workdir, j), jobs):
            print(message, flush=True)
            failed += message.startswith("FAILED")
    print(f"done, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
