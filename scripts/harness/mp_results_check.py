"""Results screen buttons and custom modes in game (docs/reference/netcode.md "Results screen"): two real instances.

    python scripts/harness/mp_results_check.py --port 27600            # buttons: Rematch, Next map, Leave, Back to lobby
    python scripts/harness/mp_results_check.py --port 27600 --custom   # instagib-lite: custom score limit, points, results

Buttons run (default): A hosts a deathmatch on Surdana Meadow (22 s time limit, results stay 120 s), B joins. Match 1 ends
by time: A presses Back to lobby (keyboard); B sees the host's buttons greyed. Both games return to the title on their own
(START, Quit to Title), the lobby counts down, match 2 starts. Match 2: A moves the mouse over Rematch and clicks it (mouse
path), again back to the title and into match 3. Match 3: B presses Leave, then A presses Next map (keyboard); A returns to
the title and the next countdown launches City of the Damned: Skeleton Square.

--custom: A hosts with Game Mode = Quick Frags (instagib-lite: 2 points a kill, score limit 20, overridden to 4 for the run),
B joins; two kills end the match on points; the results show the SCORE column. Screenshots show the Game Mode row, the
lobby's mode name, the HUD and the results.

Private play folders build/mp-res-a / -b (exe from build/final-game/bin, DLLs from dist/DrakanPC), output
build/harness/<name>/{a,b}/ (stdout_1.log, recipe.txt, *.png) and summary.json. Exit status 0 when every check passed.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import netplay_menu_two_instances as base
from netplay_menu_two_instances import ROOT, game_env, host_keys, log, prepare_folder, start, taps, text_of

SRC = base.SRC
WINDOW = base.WINDOW


def button_center(count: int, index: int) -> tuple[int, int]:
    """Script cursor position (640 x 512 units) of the results button `index` of `count` (design rects of gameui resultsButtonDesign, 1920 x 1080)."""
    w = 236.0 if count > 3 else 262.0
    gap = 14.0
    right = 110.0 + 1700.0 - 44.0
    x0 = right - count * w - (count - 1) * gap
    cx = x0 + index * (w + gap) + w / 2
    cy = 60.0 + 960.0 - 100.0 + 33.0
    # Script cursor coordinates are PS2 screen units (640 x 512, scaled to the window), not window pixels.
    return round(cx / 1920.0 * 640.0), round(cy / 1080.0 * 512.0)


MOUSE: list[bool] = [False]  # --mouse: Rematch with the mouse (cursor + click) instead of the keyboard
START_MS = 95000  # A presses Start Match this long after its title: B boots later than A on a busy PC


def host_lines(mode_steps: int) -> list[str]:
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 1500
    for keys in host_keys(0, mode_steps=mode_steps):
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 500
    lines += [f"{t} shot a_host_form", f"{t + 300} key tap ENTER", f"{t + 2500} shot a_lobby_alone"]
    for k in range(
        1, START_MS // 20000
    ):  # idle taps (Down, Up on the lobby rows): the title starts the credits when left alone
        lines += [f"{20000 * k} key tap DOWN", f"{20000 * k + 400} key tap UP"]
    lines += [f"{START_MS} shot a_lobby_with_b", f"{START_MS + 500} key tap DOWN", f"{START_MS + 1000} key tap ENTER"]
    return lines


def join_lines() -> list[str]:
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 3000
    for keys in (["DOWN", "ENTER"], ["DOWN"] * 6 + ["ENTER"]):
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 600
    lines += [
        f"{t + 2000} shot b_join_list",
        f"{t + 2300} key tap UP",
        f"{t + 3000} key tap ENTER",
        f"{t + 5200} shot b_lobby",
    ]
    return lines


def buttons_recipes() -> tuple[str, str]:
    """Match 1: A presses Back to lobby (keyboard). Match 2: A clicks Rematch (mouse). Match 3: B presses Leave, then A
    presses Next map. The results stay up 120 s; A acts after B so that B's Leave comes first."""
    a = host_lines(0)
    b = join_lines()
    rx, ry = button_center(4, 2)
    a += ["0 sync play 60", "12000 shot a_m1_playing", "30000 shot a_m1_results", "34000 key tap ENTER"]
    a += [
        "0 sync screen 8 3",
        "2500 shot a_m1_title_lobby",
        "6000 key tap DOWN",
        "6300 shot a_m1_after_down",
        "6600 key tap ENTER",
        "7500 shot a_m1_after_enter",
        "0 sync play 60",
    ]
    b += ["0 sync play 60", "12000 shot b_m1_playing", "30000 shot b_m1_results", "0 sync screen 8 3"]
    b += ["2500 shot b_m1_title_lobby", "0 sync play 60"]
    a += ["12000 shot a_m2_playing", "30000 shot a_m2_results"]
    if MOUSE[0]:  # the script's cursor units map onto the real client area: this needs the window at its set size
        a += [f"34000 cursor {rx - 40} {ry}", f"34200 cursor {rx} {ry}", "35000 shot a_m2_results_hover"]
        a += ["36000 button tap 1"]
    else:  # Right, Right: Back to lobby -> Next map -> Rematch
        a += [
            "34000 key tap RIGHT",
            "34400 key tap RIGHT",
            "35000 shot a_m2_results_rematch_focus",
            "36000 key tap ENTER",
        ]
    a += ["0 sync screen 8 3", "2500 shot a_m2_title_lobby"]
    a += ["0 sync play 60"]
    b += ["12000 shot b_m2_playing", "30000 shot b_m2_results", "0 sync screen 8 3", "2500 shot b_m2_title_lobby"]
    b += ["0 sync play 60"]
    a += [
        "12000 shot a_m3_playing",
        "30000 shot a_m3_results",
        "43000 key tap RIGHT",
        "44000 shot a_m3_results_next_focus",
    ]
    a += ["45000 key tap ENTER", "0 sync screen 8 3", "3000 shot a_m3_title_lobby", "9000 quit"]
    b += ["12000 shot b_m3_playing", "28000 shot b_m3_results", "31000 shot b_m3_results_guest", "33000 key tap ENTER"]
    b += ["0 sync screen 8 3", "3000 shot b_after_leave", "6000 quit"]
    return "\n".join(a) + "\n", "\n".join(b) + "\n"


def custom_recipes() -> tuple[str, str]:
    # The pvp flow of netplay_menu_two_instances with the custom mode picked on the Host page.
    return base.pvp_recipe("a", 0, 1, START_MS), base.pvp_recipe("b", 0, 1)


def buttons_checks(ta: str, tb: str) -> dict[str, bool]:
    c: dict[str, bool] = {}
    c["match 1 ended by time on both"] = all(re.search(r"match over \(time limit\)", t) for t in (ta, tb))
    c["A pressed Back to lobby (keyboard), the server returned everybody"] = (
        "[results] Back to lobby" in ta and "results: back to the lobby (host)" in ta
    )
    c["both games went back to the title on their own"] = all(
        t.count("host chose from the results: returning to the title") >= 1
        and "auto return to the title: the title is up" in t
        for t in (ta, tb)
    )
    c["the lobby resumed at the title on both"] = all(
        t.count("title: resumed the multiplayer lobby") >= 1 for t in (ta, tb)
    )
    c["match 2 started (Playing twice)"] = all(len(re.findall(r"phase -> 2", t)) >= 2 for t in (ta, tb))
    c["A pressed Rematch, the server took it"] = "[results] Rematch" in ta and "results: rematch (host)" in ta
    c["match 3 started (Playing three times)"] = all(len(re.findall(r"phase -> 2", t)) >= 3 for t in (ta, tb))
    c["B pressed Leave and left the session"] = "[results] Leave" in tb and bool(
        re.search(r"player \d+ 'JoinRynn' left", ta)
    )
    c["A pressed Next map, the server picked the next arena"] = "[results] Next map" in ta and bool(
        re.search(r"server: next map: City of the Damned", ta)
    )
    c["the next map is launched (base level City of the Damned)"] = bool(
        re.search(r"launch: New Game with level 'Level 1D - City of the Damned'", ta)
    )
    c["A is back in the lobby at the title three times"] = ta.count("title: resumed the multiplayer lobby") >= 3
    return c


def custom_checks(ta: str, tb: str) -> dict[str, bool]:
    c = base.pvp_flow_checks(ta, tb, 2, [])
    c = {k: v for k, v in c.items() if not k.startswith(("chat", "both returned", "second match"))}
    c["A hosted with the custom mode"] = bool(re.search(r"server: .*mode instagib-lite|mode 'instagib-lite'", ta)) or (
        "instagib-lite" in ta
    )
    c["kills scored 2 points each (score limit 4 reached by two kills)"] = len(
        re.findall(r"death: player 2 killed by player 1", ta)
    ) >= 2 and bool(re.search(r"score: player 1 kills 2 deaths 0", ta))
    c["the match ended on the score limit"] = all(re.search(r"match over \(frag limit\)", t) for t in (ta, tb))
    return c


LOGS: dict[str, Path] = {}


def open_log(p: Path) -> str:
    return text_of(p)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=27600)
    ap.add_argument("--custom", action="store_true")
    ap.add_argument("--mouse", action="store_true")
    ap.add_argument("--name", default=None)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--timeout", type=int, default=700)
    ap.add_argument("--size", default="1280x720")
    a = ap.parse_args()
    WINDOW[:] = [int(v) for v in a.size.lower().split("x")]
    MOUSE[0] = a.mouse
    base.PORT[:] = [a.port]
    name = a.name or ("mp_results_custom" if a.custom else "mp_results")
    run = ROOT / "build" / "harness" / name
    folders = {"a": ROOT / "build" / "mp-res-a", "b": ROOT / "build" / "mp-res-b"}
    exes = {k: prepare_folder(SRC, a.exe, v) for k, v in folders.items()}
    for k in ("a", "b"):
        (run / k).mkdir(parents=True, exist_ok=True)
        for stale in [*(run / k).glob("*.png"), *(run / k).glob("*.bmp")]:
            stale.unlink()
        LOGS[k] = run / k
    recipe_a, recipe_b = custom_recipes() if a.custom else buttons_recipes()
    (run / "a" / "recipe.txt").write_text(recipe_a, encoding="utf-8", newline="\n")
    (run / "b" / "recipe.txt").write_text(recipe_b, encoding="utf-8", newline="\n")
    env_a = game_env(run / "a", folders["a"], run / "a" / "recipe.txt", a.timeout, "HostRynn")
    env_b = game_env(run / "b", folders["b"], run / "b" / "recipe.txt", a.timeout, "JoinRynn")
    for e in (env_a, env_b):
        e["DRAKAN_NO_ACTIVATE"] = "0"  # the windows take the focus: "free gameplay" (sync play) needs a focused window
    env_a["DRAKAN_MP_RESULTS_SEC"] = "120"
    for e in (env_a, env_b):
        e["DRAKAN_MP_RESULTS_DEBUG"] = "1"
    if a.custom:
        env_a["DRAKAN_MP_FRAG_LIMIT"] = "4"  # the mode's 20 points would be ten kills
        for e in (env_a, env_b):
            e.update({"DRAKAN_PVP_DAMAGE_SCALE": "8", "DRAKAN_PVP_TEST_PLACE": "140", "DRAKAN_PVP_TEST_ANCHOR": "1"})
    else:
        env_a["DRAKAN_MP_TIME_LIMIT_SEC"] = "22"
    pa = start(exes["a"], env_a, run / "a")
    # B starts once A listens (boot times vary a lot on a busy PC; the LAN sweep of B's Join page runs once).
    end = time.time() + 240
    while time.time() < end and "host: listening on UDP" not in text_of(run / "a") and pa.poll() is None:
        time.sleep(1.0)
    time.sleep(1.0)
    pb = start(exes["b"], env_b, run / "b")
    deadline = time.time() + a.timeout + 90
    for p in (pa, pb):
        try:
            p.wait(timeout=max(1, deadline - time.time()))
        except subprocess.TimeoutExpired:
            p.kill()
    ta, tb = text_of(run / "a"), text_of(run / "b")
    checks = custom_checks(ta, tb) if a.custom else buttons_checks(ta, tb)
    for k, v in checks.items():
        log(f"{'ok  ' if v else 'FAIL'} {k}")
    shots = {k: sorted(x.name for x in (run / k).glob("*.png")) for k in ("a", "b")}
    (run / "summary.json").write_text(
        json.dumps({"checks": checks, "shots": shots}, indent=1), encoding="utf-8", newline="\n"
    )
    log(f"output: {run}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
