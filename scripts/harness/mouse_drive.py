"""Real-mouse look test (Windows): launch the game on a fresh card, wait for gameplay, then drive the mouse with steady SendInput motion (real Raw Input
path) while the frame audit and mouse audit log. usage: mouse_drive.py <run name> [KEY=VALUE ...]"""

import ctypes
import os
import shutil
import subprocess
import sys
import time
from ctypes import wintypes
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import winkeys as wk  # noqa: E402

ROOT = HERE.parents[1]
exe = Path(os.environ.get("EXE", str(ROOT / "dist" / "DrakanPC" / "drakan.exe")))
name = sys.argv[1]
out = ROOT / "build" / "harness" / name
shutil.rmtree(out, ignore_errors=True)
(out / "card" / "mc0").mkdir(parents=True)
env = dict(os.environ)
env.update(
    {
        "DRAKAN_LOG_DIR": str(out),
        "DRAKAN_AUTOPRESS": "3000",
        "DRAKAN_AUTOPRESS_UNTIL": "60000",
        "DRAKAN_INPUT_AUTOSTART": "1",
        "DRAKAN_MEMCARD_DIR": str(out / "card" / "mc0"),
        "DRAKAN_AUDIO": "0",
        "DRAKAN_FRAME_AUDIT": "2",
        "DRAKAN_MOUSE_AUDIT": "1",
        "PS2X_MAX_SECONDS": "90",
    }
)
for kv in sys.argv[2:]:
    k, _, v = kv.partition("=")
    env[k] = v
log = (out / "stdout.log").open("wb")
proc = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=log, stderr=subprocess.STDOUT)
hwnd = wk.find_window(proc.pid, 60)
time.sleep(float(os.environ.get("WAIT_PLAY", "40")))
wk.focus(hwnd)
time.sleep(1.0)

# Relative motion: 1000 Hz, 2 counts per ms (2000 counts/s) one way, then back, for ~14 s.
MOUSEEVENTF_MOVE = 1


class MI(ctypes.Structure):
    _fields_ = [
        ("dx", wintypes.LONG),
        ("dy", wintypes.LONG),
        ("mouseData", wintypes.DWORD),
        ("dwFlags", wintypes.DWORD),
        ("time", wintypes.DWORD),
        ("dwExtraInfo", ctypes.c_size_t),
    ]


class IN(ctypes.Structure):
    class U(ctypes.Union):
        _fields_ = [("mi", MI)]

    _anonymous_ = ("u",)
    _fields_ = [("type", wintypes.DWORD), ("u", U)]


def move(dx, dy):
    i = IN(type=0, mi=MI(dx, dy, 0, MOUSEEVENTF_MOVE, 0, 0))
    ctypes.windll.user32.SendInput(1, ctypes.byref(i), ctypes.sizeof(i))


rate_hz = int(os.environ.get("MOUSE_HZ", "1000"))
step = 2000 // rate_hz
period = 1.0 / rate_hz
t0 = time.perf_counter()
n = 0
print("driving mouse", flush=True)
while time.perf_counter() - t0 < 14.0:
    sign = 1 if int((time.perf_counter() - t0) / 3.5) % 2 == 0 else -1
    move(sign * step, 0)
    n += 1
    while time.perf_counter() < t0 + n * period:
        pass
print("done", n, "events", flush=True)
time.sleep(1)
proc.kill()
