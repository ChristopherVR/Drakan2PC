"""Multiplayer menu check (docs/reference/netcode.md "Multiplayer menu"): drives the title menu's Multiplayer pages by input script.

    python scripts/harness/netplay_menu_two_instances.py --screens      # one instance, a screenshot of every page
    python scripts/harness/netplay_menu_two_instances.py                # A hosts through the menu, B finds A in the LAN
                                                                        # list and joins, both end up in the same level

Private play folders build/mp-ui-a and build/mp-ui-b (exe and DLLs copied from dist/DrakanPC, textures / movies / nr /
ngx_data are junctions), private memory cards under build/harness/<name>/<a|b>/card/mc0. Output: build/harness/<name>/
{a,b}/ (stdout_1.log, recipe.txt, *.png) and summary.json. Exit status 0 when every check passed.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import prepare_folder

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / "dist" / "DrakanPC"
LEVELS = [
    "Level 1A - Surdana",
    "Level 1B - Kragmor",
    "Level 1C - Catacombs",
    "Level 1D - City of the Damned",
    "Level 1E - Inquisitor's Lair",
    "Level 2 - Shadowmire",
    "Level 3A - Andrellian Isles",
    "Level 3B - Crystal Mine",
]


def taps(start: int, keys: list[str], gap: int = 250) -> tuple[list[str], int]:
    """Key tap lines for `keys` from `start` ms, `gap` ms apart; returns the lines and the time after the last tap."""
    lines = []
    t = start
    for k in keys:
        lines.append(f"{t} key tap {k}")
        t += gap
    return lines, t


def screens_recipe() -> str:
    """One shot per page. Multiplayer page rows: Player Name, Color, Human Skin, Dragon Skin, Speed, Host, Join,
    mplayer.com (greyed), Back."""
    lines = ["clock milliseconds", "0 sync screen 8 3", "1500 shot title_main"]
    t = 2000
    for keys, shot in (
        (["DOWN"], "title_multiplayer_focus"),
        (["ENTER"], "mp_menu"),
        (["DOWN", "RIGHT", "RIGHT"], "mp_menu_colour"),  # Player Color
        (["DOWN"] * 4 + ["ENTER"], "mp_host"),  # Host Game
        (["DOWN"] * 3 + ["RIGHT"] * 3, "mp_host_kragmor"),  # Starting Level
        (["DOWN"] * 4 + ["RIGHT"] * 3, "mp_host_teams_coming"),  # Number of Teams: 3 shown as coming
        (["ESC"], None),
        (["DOWN"] * 6 + ["ENTER"], "mp_join_lan"),  # Join Game
        (["DOWN", "DOWN", "ENTER"], "mp_join_add_server"),  # Refresh -> Add Server (entry open)
    ):
        ls, t = taps(t, keys)
        lines += ls
        if shot:
            lines.append(f"{t + 400} shot {shot}")
            t += 900
    lines.append(f"{t + 500} quit")
    return "\n".join(lines) + "\n"


def recipe_a_text(level_index: int, walk: str, quit_at: int) -> str:
    """A: Multiplayer > Host Game > Starting Level (RIGHT x level index) > Begin Game; waits in the lobby for B."""
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 1500
    for keys in (["DOWN", "ENTER"], ["DOWN"] * 5 + ["ENTER"], ["DOWN"] * 3 + ["RIGHT"] * level_index, ["DOWN"] * 5):
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 500
    lines += [f"{t} shot a_host_form", f"{t + 300} key tap ENTER", f"{t + 2500} shot a_lobby_alone"]
    lines += ["22000 shot a_lobby_with_b", "22500 key tap DOWN", "23000 key tap ENTER", "0 sync play 60", walk]
    lines.append(f"{quit_at} quit")
    return "\n".join(lines) + "\n"


def recipe_b_text(shots: str, quit_at: int) -> str:
    """B: Multiplayer > Join Game; the table's last row (UP from Refresh) is the server; waits in the lobby."""
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 3000
    for keys in (["DOWN", "ENTER"], ["DOWN"] * 6 + ["ENTER"]):
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 600
    lines += [f"{t + 2000} shot b_join_list", f"{t + 2300} key tap UP", f"{t + 2700} shot b_join_selected"]
    lines += [f"{t + 3000} key tap ENTER", f"{t + 5200} shot b_lobby", "0 sync play 60", shots, f"{quit_at} quit"]
    return "\n".join(lines) + "\n"


def log(msg: str) -> None:
    print(f"[mp-menu-harness] {msg}", flush=True)


def game_env(out: Path, folder: Path, recipe: Path, timeout: int, name: str) -> dict[str, str]:
    card = out / "card" / "mc0"
    if card.parent.exists():
        shutil.rmtree(card.parent)
    card.mkdir(parents=True)
    options = out / "drakan_native_options.ini"
    if (folder / "drakan_native_options.ini").is_file():
        shutil.copyfile(folder / "drakan_native_options.ini", options)
    env = dict(os.environ)
    for k in list(env):
        if k.startswith("DRAKAN_NET_") or k in ("DRAKAN_START_LEVEL", "DRAKAN_GHOST", "DRAKAN_AUTOPRESS"):
            del env[k]
    env.update(
        {
            "DRAKAN_INPUT_SCRIPT": str(recipe),
            "DRAKAN_SHOT_DIR": str(out),
            "DRAKAN_LOG_DIR": str(out),
            "PS2X_MAX_SECONDS": str(timeout),
            "DRAKAN_NO_ACTIVATE": "1",
            "DRAKAN_AUDIO": "0",
            "DRAKAN_MEMCARD_DIR": str(card),
            "DRAKAN_NATIVE_OPTIONS_INI": str(options),
            "DRAKAN_NET_NAME": name,
            "DRAKAN_NET_TRACE": "1",
            "DRAKAN_LIFECYCLE_LOG": "1",
            "DRAKAN_FULLSCREEN": "0",
            "DRAKAN_WINDOW_WIDTH": "1280",
            "DRAKAN_WINDOW_HEIGHT": "720",
        }
    )
    return env


def start(exe: Path, env: dict[str, str], out: Path) -> subprocess.Popen:
    f = (out / "stdout_1.log").open("wb")
    return subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=f, stderr=subprocess.STDOUT)


def text_of(out: Path) -> str:
    p = out / "stdout_1.log"
    return p.read_bytes().decode("latin-1") if p.is_file() else ""


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--screens", action="store_true", help="one instance, a screenshot of each page")
    ap.add_argument("--name", default=None)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--timeout", type=int, default=150)
    ap.add_argument(
        "--level-index", type=int, default=0, help="map row position (0 = Surdana; others use the file redirect)"
    )
    a = ap.parse_args()
    name = a.name or ("mp_menu_screens" if a.screens else "mp_menu_two")
    run = ROOT / "build" / "harness" / name
    folders = {"a": ROOT / "build" / "mp-ui-a", "b": ROOT / "build" / "mp-ui-b"}
    exes = {k: prepare_folder(SRC, a.exe, v) for k, v in folders.items()}
    for k in ("a", "b"):
        (run / k).mkdir(parents=True, exist_ok=True)
    summary: dict[str, object] = {}

    if a.screens:
        recipe = run / "a" / "recipe.txt"
        recipe.write_text(screens_recipe(), encoding="utf-8", newline="\n")
        env = game_env(run / "a", folders["a"], recipe, a.timeout, "Rynn")
        p = start(exes["a"], env, run / "a")
        try:
            p.wait(timeout=a.timeout + 30)
        except subprocess.TimeoutExpired:
            p.kill()
        shots = sorted(x.name for x in (run / "a").glob("*.png"))
        log(f"screenshots: {', '.join(shots)}")
        summary["shots"] = shots
        ok = len(shots) >= 8
    else:
        walk = "500 key down W\n3000 key up W"
        level = LEVELS[a.level_index]
        shots_a = "\n6000 shot a_in_level\n9000 shot a_sees_b\n12500 shot a_scoreboard"
        recipe_a = recipe_a_text(a.level_index, walk + shots_a, 16000)
        recipe_b = recipe_b_text("1000 shot b_in_level\n5000 shot b_sees_a", 17000)
        (run / "a" / "recipe.txt").write_text(recipe_a, encoding="utf-8", newline="\n")
        (run / "b" / "recipe.txt").write_text(recipe_b, encoding="utf-8", newline="\n")
        # B starts later so that A's server is up and listed.
        env_a = game_env(run / "a", folders["a"], run / "a" / "recipe.txt", a.timeout, "HostRynn")
        env_a["DRAKAN_NET_SCOREBOARD"] = "9"  # the scoreboard HUD is drawn 9 s into the match (scripts cannot press F4)
        env_b = game_env(run / "b", folders["b"], run / "b" / "recipe.txt", a.timeout, "JoinRynn")
        pa = start(exes["a"], env_a, run / "a")
        time.sleep(6)
        pb = start(exes["b"], env_b, run / "b")
        deadline = time.time() + a.timeout + 60
        for p in (pa, pb):
            try:
                p.wait(timeout=max(1, deadline - time.time()))
            except subprocess.TimeoutExpired:
                p.kill()
        ta, tb = text_of(run / "a"), text_of(run / "b")
        checks = {
            "A hosts through the menu": "multiplayer action HostMatch" in ta,
            "A listens": bool(re.search(r"\[netplay [\d.]+\] host: listening on UDP", ta)),
            "B joins from the LAN list": "multiplayer action JoinMatch" in tb,
            "B connected": bool(re.search(r"\[netplay [\d.]+\] connected \(player id", tb)),
            "A saw B join": bool(re.search(r"player \d+ 'JoinRynn' joined", ta)),
            "A launched New Game": "multiplayer launch: New Game with" in ta,
            "B launched New Game": "multiplayer launch: New Game with" in tb,
            "A loaded the match level": bool(re.search(rf"level '{re.escape(level)}' loaded \(playable", ta)),
            "B loaded the match level": bool(re.search(rf"level '{re.escape(level)}' loaded \(playable", tb)),
            "A spawned B's ghost": bool(re.search(r"ghost of player \d+ 'JoinRynn' spawned", ta)),
            "B spawned A's ghost": bool(re.search(r"ghost of player \d+ 'HostRynn' spawned", tb)),
        }
        for k, v in checks.items():
            log(f"{'ok  ' if v else 'FAIL'} {k}")
        summary["checks"] = checks
        ok = all(checks.values())
    (run / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8", newline="\n")
    log(f"output: {run}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
