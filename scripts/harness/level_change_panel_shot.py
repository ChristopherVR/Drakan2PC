"""Screenshot of the F1 World panel with its "Level exits" list (drakan_levelchange::drawControls).

    python scripts/harness/level_change_panel_shot.py [--exe build/final-game/bin/drakan.exe] [--prefix lcp]

Renderer screenshots (`shot`) never contain the ImGui overlay, so this uses the overlay's own capture inbox
(DRAKAN_OVERLAY_SHOT_DIR, drakan_settings_host.cpp maybeShot): once Surdana is up and the exits were scanned, the driver
writes command.txt ("toggle" = the F1 hotkey) and then shot.txt, and the render thread writes the presented frame, overlay
included, to <out>/overlay/<name>.bmp (converted to PNG when Pillow is available). Output: build/harness/<prefix>-panel/.
Private play folder and memory card, like level_change_run.py.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "scripts" / "harness"))
import netplay_two_instances as net  # noqa: E402


def wait_for(path: Path, timeout: float) -> bool:
    end = time.monotonic() + timeout
    while time.monotonic() < end:
        if path.exists():
            return True
        time.sleep(0.5)
    return False


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--prefix", default="lcp")
    ap.add_argument("--timeout", type=int, default=420)
    a = ap.parse_args()
    name = f"{a.prefix}-panel"
    out = ROOT / "build" / "harness" / name
    exe = net.prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-play")
    recipe = ROOT / "build" / "harness" / f"{name}-recipe.txt"
    recipe.parent.mkdir(parents=True, exist_ok=True)
    recipe.write_text("clock milliseconds\n0 sync play 60\n200000 quit\n", encoding="utf-8", newline="\n")
    overlay = out / "overlay"
    cmd = [
        sys.executable, str(ROOT / "scripts" / "harness" / "play_run.py"),
        "--name", name, "--exe", str(exe), "--recipe", str(recipe), "--timeout", str(a.timeout), "--attempts", "1",
        "--env", "DRAKAN_WORLD=1", "--env", f"DRAKAN_OVERLAY_SHOT_DIR={overlay}",
        "--env", "DRAKAN_LEVELCHANGE=99@100000000", "--env", "DRAKAN_LIFECYCLE_LOG=1",
    ]  # fmt: skip
    proc = subprocess.Popen(cmd, cwd=ROOT, stdout=subprocess.DEVNULL)
    log = out / "stdout_1.log"
    try:
        # the level is up and its exits were listed (frame 3 of the second level load)
        end = time.monotonic() + a.timeout
        while time.monotonic() < end:
            text = log.read_text(encoding="latin-1") if log.is_file() else ""
            if text.count("player block:") >= 2 and "arrival report 'Level 1A - Surdana' frame 200" in text:
                break
            time.sleep(1)
        else:
            print("Surdana never came up")
            return 1
        overlay.mkdir(parents=True, exist_ok=True)
        time.sleep(3)
        (overlay / "command.txt").write_text("p1 toggle\n")
        if not wait_for(overlay / "ack.txt", 60):
            print("no ack for the F1 toggle")
            return 1
        time.sleep(4)  # a few overlay frames: the World window samples and draws the exit list
        (overlay / "shot.txt").write_text("level_exits\n")
        ok = wait_for(overlay / "level_exits.bmp", 60)
        time.sleep(1)
        print("overlay shot:", overlay / "level_exits.bmp" if ok else "not written")
        if ok:
            try:
                from PIL import Image

                png = overlay / "level_exits.png"
                Image.open(overlay / "level_exits.bmp").convert("RGB").save(png)
                print("png:", png)
            except ImportError:
                pass
        return 0 if ok else 1
    finally:
        # stop what this script started, by PID: the game is the child of play_run.py
        kids = subprocess.run(
            ["powershell", "-NoProfile", "-Command",
             f"Get-CimInstance Win32_Process -Filter 'ParentProcessId={proc.pid}' | ForEach-Object {{ $_.ProcessId }}"],
            capture_output=True, text=True,
        ).stdout.split()  # fmt: skip
        for pid in kids:
            subprocess.run(["taskkill", "/PID", pid, "/F"], capture_output=True)
        proc.terminate()
        try:
            proc.wait(timeout=10)
        except subprocess.TimeoutExpired:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
