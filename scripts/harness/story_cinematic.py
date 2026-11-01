"""Cinematic harness: scripted scenes (SSD sequences) played, edited and created, with asserted checks.

    python scripts/harness/story_cinematic.py                 # all phases
    python scripts/harness/story_cinematic.py --phases stock,edit
    python scripts/harness/story_cinematic.py --exe build/final-game/bin/drakan.exe

Phases (each game run is a play_run.py run in a private play folder, output build/harness/cine-*):
  stock  Valley of the Fallen: the entry cut scene (cut scene player 7577, Initial State 1, sequence 497) plays at level load.
         Asserts: the sequence was requested (HLE log), the HUD is hidden, the camera cuts (frames differ).
  edit   The same scene with a pack made by the editor's own code (ContentSession.editSequence ops): camera key 0 re-aimed,
         the camera track retimed x2.5, one of Zola Dane's animations swapped. Asserts: the pack loaded and rebuilt the SSD,
         the first frame differs from stock (camera pose), and the stock cut (seen at frame 200) has not happened yet.
  new    Surdana: a NEW cut scene (camera points, sound step, trigger volume + player from the logic template). A volume away
         from Rynn's start: negative run (player outside) shows plain gameplay, positive run (DRAKAN_PLAYTEST_POS teleports the
         player into the volume) plays the scene, then control returns. Asserts: new sequence requested, frames differ from the
         negative run while the scene plays, and the last frame is gameplay again (HUD back, third-person view).

Camera key orientation (settled 2026-11-01): the key quaternion is a row-vector rotation, the camera looks along its -Z;
`yawPitchQuat` in editor-core encodes that (docs/reference/level-format.md section 14). Packs are built by
scripts/harness/story_cinematic/pack.mjs (node --experimental-transform-types, needs the player's ISO through drakan.ini or
DRAKAN_ISO). Screenshots and logs stay under build/.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
HARNESS = ROOT / "build" / "harness"
PLAY = ROOT / "build" / "cine-play"
PACK_JS = ROOT / "scripts" / "harness" / "story_cinematic" / "pack.mjs"
FAILS: list[str] = []


def check(ok: bool, what: str) -> None:
    print(("PASS " if ok else "FAIL ") + what)
    if not ok:
        FAILS.append(what)


def ensure_play_folder(exe: Path) -> Path:
    PLAY.mkdir(parents=True, exist_ok=True)
    dist = ROOT / "dist" / "DrakanPC"
    for pat in ("*.dll", "*.ini"):
        for f in dist.glob(pat):
            shutil.copyfile(f, PLAY / f.name)
    shutil.copyfile(exe, PLAY / "drakan.exe")
    return PLAY / "drakan.exe"


def build_pack(name: str, spec: dict) -> Path:
    out = ROOT / "build" / "cine" / name
    shutil.rmtree(out, ignore_errors=True)
    out.parent.mkdir(parents=True, exist_ok=True)
    spec_path = out.parent / f"{name}.json"
    spec_path.write_text(json.dumps(spec), encoding="utf-8")
    r = subprocess.run(
        ["node", "--experimental-transform-types", str(PACK_JS), str(spec_path), str(out)],
        capture_output=True,
        text=True,
        cwd=ROOT,
    )
    if r.returncode != 0:
        raise SystemExit(f"pack build failed:\n{r.stderr[-2000:]}")
    print("pack", name, r.stdout.strip().splitlines()[-1][:300])
    return out


def run_game(
    name: str,
    exe: Path,
    shots: list[int],
    level: str | None = None,
    pack: Path | None = None,
    playtest: str | None = None,
    timeout: int = 500,
) -> Path:
    cmd = [
        sys.executable,
        str(ROOT / "scripts" / "harness" / "play_run.py"),
        "--name",
        name,
        "--exe",
        str(exe),
        "--shots",
        ",".join(map(str, shots)),
        "--timeout",
        str(timeout),
        "--attempts",
        "2",
        "--env",
        "DRAKAN_HLE_LOG=2000000",
    ]
    if level:
        cmd += ["--level", level]
    if pack:
        cmd += ["--env", f"DRAKAN_MODS={pack}"]
    if playtest:
        cmd += ["--env", f"DRAKAN_PLAYTEST_POS={playtest}"]
    r = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    if r.returncode != 0:
        raise SystemExit(f"game run {name} failed:\n{r.stdout[-1500:]}")
    return HARNESS / name


def shot(folder: Path, frame: int) -> np.ndarray:
    f = next(folder.glob(f"shot*_{frame}.png"))
    return np.asarray(Image.open(f).convert("L").resize((96, 54)), dtype=np.float32)


def diff(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.abs(a - b).mean())


def hud_present(folder: Path, frame: int) -> bool:
    """The health orb (bright red blob) at the bottom left of the frame."""
    f = next(folder.glob(f"shot*_{frame}.png"))
    im = np.asarray(Image.open(f).convert("RGB"), dtype=np.int32)
    h, w, _ = im.shape
    reg = im[int(h * 0.86) :, : int(w * 0.1)]
    red = (reg[..., 0] > 150) & (reg[..., 1] < 70) & (reg[..., 2] < 70)
    return float(red.mean()) > 0.01


def log_text(folder: Path) -> str:
    return (folder / "drakan.log").read_text(encoding="latin-1", errors="replace")


def phase_stock(exe: Path) -> Path:
    print("== stock: Valley entry scene")
    d = run_game("cine-h-stock", exe, [30, 200, 900], level="valley")
    log = log_text(d)
    check("LEVEL6EN.SSD 0311/497" in log, "sequence 497 requested from LEVEL6EN.SSD")
    check(not any(hud_present(d, f) for f in (30, 200, 900)), "HUD hidden during the cut scene (3 frames)")
    check(diff(shot(d, 30), shot(d, 200)) > 15, "camera cut between frame 30 and 200")
    check(diff(shot(d, 30), shot(d, 900)) > 15, "aerial establishing shot (frame 30) versus throne room (frame 900)")
    return d


def phase_edit(exe: Path, stock: Path) -> None:
    print("== edit: camera pose, timing, animation")
    spec = {
        "id": "cine.edit",
        "level": "LEVEL6EN",
        "story": "LEVEL6EN",
        "label": "Valley",
        "edits": [
            {
                "sequence": 497,
                "ops": [
                    {
                        "op": "pose",
                        "actor": "Camera",
                        "nth": 0,
                        "pos": [577042, 288000, 125491],
                        "yaw": 30,
                        "pitch": 55,
                    },
                    {"op": "timescale", "actor": "Camera", "factor": 2.5},
                    {"op": "set", "actor": "Zola Dane", "type": 1, "nth": 1, "field": "animation", "value": 5907},
                ],
            }
        ],
    }
    pack = build_pack("edit", spec)
    d = run_game("cine-h-edit", exe, [30, 200, 900], level="valley", pack=pack)
    log = log_text(d)
    check("mods: loaded 'cine.edit'" in log and "1 entries" in log, "pack loaded with the edited sequence entry")
    check(diff(shot(d, 30), shot(stock, 30)) > 12, "edited camera pose: first frame differs from stock")
    check(
        diff(shot(d, 200), shot(d, 30)) < diff(shot(stock, 200), shot(stock, 30)) * 0.8,
        "retimed camera: the cut seen in stock at frame 200 has not happened yet",
    )
    check(not hud_present(d, 30), "edited scene still runs as a cut scene (HUD hidden)")


def phase_new(exe: Path) -> None:
    print("== new: Surdana scene created by the editor, started by a trigger volume")
    vol = [452800.0, 171700.0, 340186.0]
    cam = [
        {"time": 0, "pos": [vol[0] + 2600, vol[1] + 900, vol[2] + 300], "yaw": -90, "pitch": 5},
        {"time": 4, "pos": [vol[0] + 2600, vol[1] + 900, vol[2] + 300], "yaw": -90, "pitch": 5},
        {"time": 4.02, "pos": [vol[0], vol[1] + 6000, vol[2]], "yaw": 0, "pitch": 85},
        {"time": 8, "pos": [vol[0], vol[1] + 6000, vol[2]], "yaw": 0, "pitch": 85},
    ]
    spec = {
        "id": "cine.new",
        "level": "LEVEL1NA",
        "story": "LEVEL1ON",
        "label": "Surdana",
        "scene": {
            "name": "Harness fly-by",
            "camera": cam,
            "duration": 9,
            "steps": [{"kind": "sound", "time": 0.5, "soundRef": 33}],
        },
        "trigger": {"at": vol, "w": 1, "h": 1, "d": 1, "once": True, "who": "2"},
    }
    pack = build_pack("new", spec)
    out = vol[:]
    out[0] -= 4000
    shots = [100, 200, 300, 400, 1800]
    neg = run_game("cine-h-new-outside", exe, shots, pack=pack, playtest=f"{out[0]} {out[1]} {out[2]} -1.5708")
    pos = run_game("cine-h-new-inside", exe, shots, pack=pack, playtest=f"{vol[0]} {vol[1]} {vol[2]} -1.5708")
    check(
        bool([x for x in log_text(pos).splitlines() if "LEVEL1ON.SSD 0311/1411" in x]),
        "new sequence 1411 read from the pack",
    )
    check("placed after" in log_text(pos), "play-test teleport placed the player")
    d_scene = [diff(shot(neg, f), shot(pos, f)) for f in shots[:4]]
    print("  frame diffs inside vs outside:", [round(x, 1) for x in d_scene])
    check(sum(x > 18 for x in d_scene) >= 2, "scene frames differ from plain gameplay (volume entered)")
    check(hud_present(neg, 1800) and hud_present(pos, 1800), "HUD present at the end of both runs")
    check(diff(shot(pos, 1800), shot(neg, 1800)) < 30, "scene over: last frame is gameplay again")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--phases", default="stock,edit,new")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    a = ap.parse_args()
    exe = ensure_play_folder(a.exe)
    phases = a.phases.split(",")
    stock = HARNESS / "cine-h-stock"
    if "stock" in phases:
        stock = phase_stock(exe)
    if "edit" in phases:
        phase_edit(exe, stock)
    if "new" in phases:
        phase_new(exe)
    print("FAILED: " + "; ".join(FAILS) if FAILS else "all checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    os.environ.setdefault("PYTHONIOENCODING", "utf-8")
    sys.exit(main())
