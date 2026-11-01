"""Host / Join / lobby pages with the real map pictures and (co-op) the rotating character model: two real instances,
screenshots only (no match is started).

    python scripts/harness/mp_pages_check.py --port 28700 --previews build/harness/prevcache            # deathmatch
    python scripts/harness/mp_pages_check.py --port 28700 --previews build/harness/prevcache --coop     # co-op + character

A hosts (Multiplayer > Host Game > Begin Game), B joins from the LAN list. Shots: a_host_form, b_join_list, b_join_selected,
b_lobby, a_lobby_with_b (and b_lobby_character with --coop: B steps the Character row, DRAKAN_COOP_CHARACTER preselects a
character whose turntable is in <previews>/chars). Private play folders build/mp-pages-a / -b; output
build/harness/<name>/{a,b}/. The pictures come from DRAKAN_PREVIEW_DIR (--previews); nothing is captured here.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import netplay_menu_two_instances as base
from netplay_menu_two_instances import ROOT, game_env, host_keys, log, prepare_folder, start, taps, text_of

LOBBY_MS = 70000  # A's lobby shot once B has joined (boot times vary on a busy PC)


def recipe_a(coop: bool, level_index: int) -> str:
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 1500
    groups = host_keys(0)
    if coop:  # New Game > Co-op opens the Multiplayer pages in the Co-op scope: Host Game, the Map row, Begin Game
        groups = [
            ["ENTER"],
            ["DOWN", "ENTER"],
            ["DOWN"] * 5 + ["ENTER"],
            ["DOWN"] * 3 + ["RIGHT"] * level_index,
            ["DOWN"] * 2,
        ]
    for keys in groups:
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 500
    lines += [f"{t} shot a_host_form", f"{t + 300} key tap ENTER", f"{t + 2500} shot a_lobby_alone"]
    for k in range(1, LOBBY_MS // 20000 + 1):  # idle taps: a title left alone starts the credits
        lines += [f"{20000 * k} key tap DOWN", f"{20000 * k + 400} key tap UP"]
    lines += [f"{LOBBY_MS} shot a_lobby_with_b", f"{LOBBY_MS + 3000} quit"]
    return "\n".join(lines) + "\n"


def recipe_b(coop: bool) -> str:
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 3000
    groups = [["DOWN", "ENTER"], ["DOWN"] * 6 + ["ENTER"]]
    if coop:
        groups = [["ENTER"], ["DOWN", "ENTER"], ["DOWN"] * 6 + ["ENTER"]]
    for keys in groups:
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 600
    for _ in range(3):  # the LAN sweep is slow on a busy PC: refresh (Refresh has the focus) and wait for the answer
        t += 8000
        lines.append(f"{t} key tap ENTER")
    t += 6000
    lines += [f"{t + 2000} shot b_join_list", f"{t + 2300} key tap UP", f"{t + 2700} shot b_join_selected"]
    lines += [f"{t + 3000} key tap ENTER", f"{t + 6000} shot b_lobby"]
    end = t + 6000
    if coop:
        lines += [f"{end + 400} key tap UP", f"{end + 800} key tap RIGHT"]
        lines += [f"{end + 2500 + 1000 * i} shot b_lobby_character_{i}" for i in range(4)]
        end += 6000
    for k in range(1, 4):  # idle taps while A waits
        lines += [f"{end + 15000 * k} key tap DOWN", f"{end + 15000 * k + 400} key tap UP"]
    lines += [f"{LOBBY_MS + 1500} shot b_lobby_late", f"{LOBBY_MS + 3000} quit"]
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=28700)
    ap.add_argument("--coop", action="store_true")
    ap.add_argument("--level-index", type=int, default=0)
    ap.add_argument("--char", default="ee7", help="co-op: DRAKAN_COOP_CHARACTER (hex class id)")
    ap.add_argument("--name", default=None)
    ap.add_argument("--previews", type=Path, default=ROOT / "build" / "harness" / "prevcache")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--timeout", type=int, default=200)
    ap.add_argument("--size", default="1280x720")
    a = ap.parse_args()
    base.WINDOW[:] = [int(v) for v in a.size.lower().split("x")]
    base.PORT[:] = [a.port]
    run = ROOT / "build" / "harness" / (a.name or ("mp_pages_coop" if a.coop else "mp_pages"))
    folders = {"a": ROOT / "build" / "mp-pages-a", "b": ROOT / "build" / "mp-pages-b"}
    exes = {k: prepare_folder(base.SRC, a.exe, v) for k, v in folders.items()}
    for k in ("a", "b"):
        (run / k).mkdir(parents=True, exist_ok=True)
        for stale in [*(run / k).glob("*.png"), *(run / k).glob("*.bmp")]:
            stale.unlink()
    (run / "a" / "recipe.txt").write_text(recipe_a(a.coop, a.level_index), encoding="utf-8", newline="\n")
    (run / "b" / "recipe.txt").write_text(recipe_b(a.coop), encoding="utf-8", newline="\n")
    env_a = game_env(run / "a", folders["a"], run / "a" / "recipe.txt", a.timeout, "HostRynn")
    env_b = game_env(run / "b", folders["b"], run / "b" / "recipe.txt", a.timeout, "JoinRynn")
    for e in (env_a, env_b):
        e["DRAKAN_PREVIEW_DIR"] = str(a.previews.resolve()) + "/"
        e["DRAKAN_MP_CAPTURE"] = "0"
    if a.coop:
        env_b["DRAKAN_COOP_CHARACTER"] = a.char
    pa = start(exes["a"], env_a, run / "a")
    end = time.time() + 240
    while time.time() < end and "host: listening on UDP" not in text_of(run / "a") and pa.poll() is None:
        time.sleep(1.0)
    time.sleep(1.0)
    pb = start(exes["b"], env_b, run / "b")
    deadline = time.time() + a.timeout + 120
    for p in (pa, pb):
        try:
            p.wait(timeout=max(1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            p.kill()
    shots = {k: sorted(x.name for x in (run / k).glob("*.png")) for k in ("a", "b")}
    log(f"screenshots: {shots}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
