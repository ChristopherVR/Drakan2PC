"""Record the game window in real time while a harness recipe plays.

The recipe writes `shot rec_start` where recording should begin; this script waits for that file, finds the
DrakanWindow client area and runs FFmpeg gdigrab for --seconds.

    python scripts/harness/record_window.py --name rec-fps --recipe R --seconds 8 --out build/x.mp4 [--save C] [--env K=V]...
"""

import argparse
import ctypes
import ctypes.wintypes as wt
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
user32 = ctypes.windll.user32
ctypes.windll.shcore.SetProcessDpiAwareness(2)


def client_rect():
    hwnd = user32.FindWindowW("DrakanWindow", None)
    if not hwnd:
        return None
    r = wt.RECT()
    user32.GetClientRect(hwnd, ctypes.byref(r))
    p = wt.POINT(0, 0)
    user32.ClientToScreen(hwnd, ctypes.byref(p))
    user32.SetForegroundWindow(hwnd)
    return p.x, p.y, r.right - r.left, r.bottom - r.top


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True)
    ap.add_argument("--recipe", required=True)
    ap.add_argument("--seconds", type=float, required=True)
    ap.add_argument("--out", required=True)
    ap.add_argument("--fps", type=int, default=60)
    ap.add_argument("--ffmpeg", default="ffmpeg", help="FFmpeg executable")
    ap.add_argument("--save")
    ap.add_argument("--level")
    ap.add_argument("--env", action="append", default=[])
    a = ap.parse_args()
    cmd = [
        sys.executable,
        str(ROOT / "scripts/harness/play_run.py"),
        "--name",
        a.name,
        "--recipe",
        a.recipe,
        "--attempts",
        "1",
        "--env",
        "DRAKAN_WINDOW_POS=40,40",
        "--env",
        "DRAKAN_PAUSE_UNFOCUSED=0",
    ]
    if a.save:
        cmd += ["--save", a.save]
    if a.level:
        cmd += ["--level", a.level]
    for kv in a.env:
        cmd += ["--env", kv]
    marker = ROOT / "build/harness" / a.name / "rec_start.png"
    game = subprocess.Popen(cmd)
    t0 = time.monotonic()
    while not marker.exists():
        if game.poll() is not None or time.monotonic() - t0 > 240:
            print("no rec_start marker", file=sys.stderr)
            game.kill()
            return 1
        time.sleep(0.02)
    x, y, w, h = client_rect()
    Path(a.out).parent.mkdir(parents=True, exist_ok=True)
    subprocess.run(
        [
            a.ffmpeg,
            "-hide_banner",
            "-loglevel",
            "error",
            "-y",
            "-f",
            "gdigrab",
            "-framerate",
            str(a.fps),
            "-offset_x",
            str(x),
            "-offset_y",
            str(y),
            "-video_size",
            f"{w}x{h}",
            "-draw_mouse",
            "0",
            "-i",
            "desktop",
            "-t",
            str(a.seconds),
            "-c:v",
            "libx264",
            "-preset",
            "veryfast",
            "-crf",
            "16",
            "-pix_fmt",
            "yuv420p",
            a.out,
        ],
        check=True,
    )
    game.wait()
    print(f"recorded {w}x{h} at {x},{y} -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
