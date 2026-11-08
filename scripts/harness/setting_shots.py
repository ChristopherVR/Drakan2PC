"""Make the real pictures of the PC Settings help panel from the player's own game.

    python scripts/harness/setting_shots.py                      # every picture that is missing
    python scripts/harness/setting_shots.py --only fov           # one setting
    python scripts/harness/setting_shots.py --force              # all again

One game run per scene. After the scene has settled, the input script changes a setting with `setting <key> <value>`
(no menu), saves the window a few samples later, changes it again, and so on, so every picture of a setting comes from
the same moment of the same scene and differs only by the setting. Rynn plays random idle gestures: a picture is kept
only when she stood still between its two shots, anything else is made again in another run. Scenes: the meadow where
Rynn starts (field of view, aspect, HUD, filtering, shadows, vegetation) and the pond (water, reflections).

Pictures are saved as previews/settings/<row key>/<value>.webp (1280 x 720, lossy) next to the executable; the panel
(gameui/SettingPreview.h) shows the one for the value a row has right now. They are made from the player's disc and never
committed. Needs Pillow (the game saves 3840 x 2160 PNGs and Pillow writes WebP). Settings with no visible effect on a
still picture have none on purpose, and neither do values like Auto aspect that depend on the window.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from dual_wield_check import patch_ini
from play_run import ROOT

WIDTH, HEIGHT = 1280, 720
SETTLE = 14  # samples between a `setting` and its first picture (the picture stays up for the second one)
GAP = 10  # samples between the two pictures of a value
START = 420  # samples after the level starts: the opening quest message is gone, Rynn has not gestured yet
POND = ("382176", "163000", "522638", "1.571")  # the Surdana pond, looking across the water
MEADOW = None  # where Rynn starts: trees, grass, a tent at the edge (no placement)
STILL = 1.6  # mean change (0..255) of the middle of the frame between a value's two pictures when Rynn stands still
EFFECT = 0.12  # a ray effect that is on must change the picture at least this much against off (else not ready yet)

BASELINE = {
    "aspect": "16:9",
    "hud_scale": "1",
    "fov": "80",
    "scale": "1",
    "filter": "game",
    "anisotropic": "1",
    "hud_aspect_correct": "on",
    "hd_textures": "on",
    "modern_water": "off",
    "ray_shadows": "off",
    "ray_reflections": "off",
    "ray_gi": "off",
    "foliage_wind": "off",
    "foliage_wind_strength": "1",
}
ONOFF = {"off": "0", "on": "1"}
RAYS = ("ray_shadows", "ray_reflections", "ray_gi", "modern_water")

# scene -> (place, [(setting, values, settings to set before the group)])
SCENES = {
    "meadow": (
        MEADOW,
        [
            ("aspect", ["4:3", "16:9", "21:9", "stretch"], {}),
            ("fov", [str(d) for d in range(60, 121)], {}),
            ("hud_scale", ["auto", "0.5", "0.6", "0.7", "0.8", "0.9", "1", "1.25", "1.5"], {}),
            ("hud_aspect_correct", ["off", "on"], {"aspect": "21:9"}),
            ("scale", ["auto", "1", "2", "3", "4"], {}),
            ("filter", ["game", "point", "bilinear"], {}),
            ("anisotropic", ["1", "2", "4", "8", "16"], {"filter": "bilinear"}),
            ("hd_textures", ["off", "on"], {}),
            ("foliage_wind", ["off", "on"], {}),
            ("foliage_wind_strength", ["0.25", "0.5", "1", "1.5", "2"], {"foliage_wind": "on"}),
            ("ray_shadows", ["off", "on"], {}),
            ("ray_gi", ["off", "on"], {}),
        ],
    ),
    "pond": (
        POND,
        [
            ("modern_water", ["off", "on"], {}),
            ("ray_reflections", ["off", "on"], {}),
        ],
    ),
}


def file_value(value: str) -> str:
    return value.replace(":", "x")


def label(key: str, value: str, part: str) -> str:
    return re.sub(r"[^A-Za-z0-9]", "_", f"{key}_{value}_{part}")


def ini_value(value: str) -> str:
    return ONOFF.get(value, value)


BASELINE_INI = {k: ini_value(v) for k, v in BASELINE.items()}


def recipe_for(place, todo: list[tuple[str, list[str], dict]]) -> str:
    """The input script of one run: every wanted (setting, value) once, each followed by two pictures."""
    lines = ["0 sync play 80"]
    if place:
        lines.append(f"20 place {' '.join(place)}")
    t = START
    for key, values, before in todo:
        for k, v in before.items():
            lines.append(f"{t} setting video.{k} {ini_value(v)}")
            t += SETTLE
        for i, value in enumerate(values):
            lines.append(f"{t} setting video.{key} {ini_value(value)}")
            # a ray effect that was just switched on builds its pass in the background: give it time
            t += 900 if key in RAYS and value == "on" else SETTLE
            lines.append(f"{t} shot {label(key, value, 'a')}")
            t += GAP
            lines.append(f"{t} shot {label(key, value, 'b')}")
            t += 2
        lines.append(f"{t} setting video.{key} {ini_value(BASELINE[key])}")
        for k in before:
            lines.append(f"{t} setting video.{k} {ini_value(BASELINE[k])}")
        t += SETTLE
    lines.append(f"{t + 60} quit")
    return "\n".join(lines) + "\n"


def run_scene(exe: Path, name: str, place, todo, workdir: Path) -> Path | None:
    """One game run of a scene; returns the folder with the pictures, None if the game did not finish."""
    run = workdir / name
    shutil.rmtree(run, ignore_errors=True)
    (run / "card/mc0").mkdir(parents=True)
    ini = patch_ini(
        (exe.parent / "drakan.ini").read_text(encoding="utf-8"),
        {
            "general": {"title_menu": "native", "pause_menu": "native", "pause_when_unfocused": "0"},
            "video": {
                **BASELINE_INI,
                "window_width": str(WIDTH),
                "window_height": str(HEIGHT),
                "show_fps": "0",
            },
        },
    )
    (run / "drakan.ini").write_text(ini, encoding="utf-8")
    shutil.copy(exe.parent / "drakan_input.ini", run / "drakan_input.ini")
    (run / "recipe.txt").write_text(recipe_for(place, todo))
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
        PS2X_MAX_SECONDS="900",
    )
    for var in ("DRAKAN_WS_ASPECT", "DRAKAN_HUD_SCALE", "DRAKAN_TITLE_MENU", "DRAKAN_PAUSE_MENU"):
        env.pop(var, None)
    with (run / "stdout.log").open("wb") as log:
        proc = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
        try:
            proc.wait(timeout=1000)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()
            return None
    text = (run / "stdout.log").read_bytes().decode("latin-1")
    return run if "script quit" in text else None


def middle(im):
    """The middle of the frame, where Rynn stands, small enough to compare quickly."""
    w, h = im.size
    return im.crop((int(w * 0.3), int(h * 0.2), int(w * 0.7), int(h * 0.95))).resize((256, 256))


def change(a, b) -> float:
    from PIL import ImageChops, ImageStat

    return sum(ImageStat.Stat(ImageChops.difference(middle(a), middle(b))).mean) / 3


def message_shown(im) -> bool:
    """The quest message of the level start (white text and a book icon, top left)."""
    w, h = im.size
    box = im.crop((int(w * 0.12), int(h * 0.015), int(w * 0.69), int(h * 0.09))).resize((320, 40))
    return box.convert("L").point(lambda v: 255 if v > 205 else 0).histogram()[255] > 4


def collect(run: Path, todo, out_root: Path) -> list[tuple[str, str]]:
    """Saves the pictures that are fine as WebP; returns the (setting, value) pairs that must be made again."""
    from PIL import Image

    again = []
    taken: dict[str, dict[str, Image.Image]] = {}
    for key, values, _ in todo:
        for value in values:
            a, b = run / f"{label(key, value, 'a')}.png", run / f"{label(key, value, 'b')}.png"
            if not (a.is_file() and b.is_file()):
                again.append((key, value))
                continue
            ia, ib = Image.open(a).convert("RGB"), Image.open(b).convert("RGB")
            if message_shown(ib) or change(ia, ib) > STILL:
                again.append((key, value))
                continue
            taken.setdefault(key, {})[value] = ib
    for key, pictures in taken.items():
        if key in RAYS and "off" in pictures and "on" in pictures and change(pictures["off"], pictures["on"]) < EFFECT:
            again.append((key, "on"))  # the effect was not ready yet
            pictures.pop("on")
    for key, pictures in taken.items():
        for value, im in pictures.items():
            dest = out_root / f"video.{key}" / f"{file_value(value)}.webp"
            dest.parent.mkdir(parents=True, exist_ok=True)
            im.resize((WIDTH, HEIGHT), Image.LANCZOS).save(dest, "WEBP", quality=88, method=6)
            print(f"saved video.{key}={value}", flush=True)
    return again


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", default=str(ROOT / "dist/DrakanPC/drakan.exe"))
    ap.add_argument("--out", default=str(ROOT / "dist/DrakanPC/previews/settings"))
    ap.add_argument("--only", default="", help="comma separated settings (fov, aspect, ray_gi ...)")
    ap.add_argument("--force", action="store_true", help="make the picture again even when it exists")
    ap.add_argument("--passes", type=int, default=5, help="game runs per scene (pictures that failed are made again)")
    args = ap.parse_args()
    try:
        import PIL  # noqa: F401
    except ImportError:
        print("setting_shots.py needs Pillow (python -m pip install pillow)")
        return 2
    exe, out_root = Path(args.exe), Path(args.out)
    only = {k.removeprefix("video.") for k in args.only.split(",") if k}
    workdir = ROOT / "build/harness/setting-shots"
    workdir.mkdir(parents=True, exist_ok=True)
    failed = 0
    for scene, (place, groups) in SCENES.items():
        todo = []
        for key, values, before in groups:
            if only and key not in only:
                continue
            missing = [
                v for v in values if args.force or not (out_root / f"video.{key}" / f"{file_value(v)}.webp").exists()
            ]
            if missing:
                todo.append((key, missing, before))
        for attempt in range(args.passes):
            if not todo:
                break
            print(f"{scene}: pass {attempt + 1}, {sum(len(v) for _, v, _ in todo)} pictures", flush=True)
            run = run_scene(exe, f"{scene}_{attempt}", place, todo, workdir)
            if run is None:
                print("the game did not finish; trying again", flush=True)
                continue
            again = collect(run, todo, out_root)
            todo = [
                (key, [v for k, v in again if k == key], before)
                for key, _values, before in todo
                if any(k == key for k, _ in again)
            ]
        failed += sum(len(v) for _, v, _ in todo)
        for key, values, _ in todo:
            print(f"FAILED video.{key}: {', '.join(values)}")
    print(f"done, {failed} failed")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
