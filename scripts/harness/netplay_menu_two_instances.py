"""Multiplayer menu check (docs/reference/netcode.md "Multiplayer menu"): drives the title menu's Multiplayer pages by input script.

    python scripts/harness/netplay_menu_two_instances.py --screens      # one instance, a screenshot of every page
    python scripts/harness/netplay_menu_two_instances.py                # A hosts through the menu, B finds A in the LAN
                                                                        # list and joins, both end up in the same level
    python scripts/harness/netplay_menu_two_instances.py --pvp          # the whole flow: menu, deathmatch with a 3 kill
                                                                        # limit, kill feed / scoreboard / chat, results,
                                                                        # back to the lobby, the next match

Private play folders build/mp-ui-a and build/mp-ui-b (build/mp-full-a / -b with --pvp; exe and DLLs copied from
dist/DrakanPC, textures / movies / nr / ngx_data are junctions), private memory cards under
build/harness/<name>/<a|b>/card/mc0. Output: build/harness/<name>/{a,b}/ (stdout_1.log, recipe.txt, *.png) and
summary.json. Exit status 0 when every check passed.

--pvp: the input script cannot press F4 / F6 or type, so a thread of this script sends real keyboard input to the game
windows (scripts/harness/winkeys.py, SendInput; the window is focused first): B types a chat line with F6 + text + Enter,
A answers, A holds F4 for the scoreboard. The thread takes its timing from the games' own logs. Test aids set for the
run: DRAKAN_MP_FRAG_LIMIT=3, DRAKAN_PVP_DAMAGE_SCALE=8 (a kill takes four dagger hits), DRAKAN_PVP_TEST_PLACE / _ANCHOR
(each game holds the other's ghost 140 units in front of / behind its own Rynn so scripted swings connect).
"""

from __future__ import annotations

import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import threading
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
# The Host page's map list for a deathmatch (configs/mp_maps.ini, in file order): (map name, base level).
ARENAS = [
    ("Surdana Meadow", "Level 1A - Surdana"),
    ("City of the Damned: Skeleton Square", "Level 1D - City of the Damned"),
    ("Inquisitor's Lair: The Depths", "Level 1E - Inquisitor's Lair"),
]


def host_keys(arena_index: int = 0, coop: bool = False, level_index: int = 0, mode_steps: int = 0) -> list[list[str]]:
    """Key groups from the title menu to Begin Game (the last group ends on the Begin Game row). Row model of the Host
    page: Server Name, Password, Level Group, Map, Game Type, Game Mode, Max Players, Kills, Time, Teams, [greyed rows],
    Begin Game. `mode_steps`: Right presses on the Game Mode row (0 = Standard, 1 = the first custom mode that fits the
    map); a custom mode fixes Kills / Time / Teams (greyed, focus skips them).
    The Map row is a list: Left / Right step it, Select enters it so that Up / Down step through the maps (Select or
    Back leaves it); outside the list Up / Down move between rows. Co-op lists the campaign levels (the Game Type
    switch resets the list, so the level is picked after it) and greys Kills / Time / Teams out (focus skips them)."""
    groups = [["DOWN", "ENTER"], ["DOWN"] * 5 + ["ENTER"], ["DOWN"] * 3]  # Multiplayer, Host Game, the Map row
    if coop:
        groups += [["DOWN", "RIGHT", "UP"] + ["RIGHT"] * level_index, ["DOWN"] * 3]  # Game Type: Co-op; Map; Begin Game
    else:
        if arena_index:
            groups.append(["ENTER"] + ["DOWN"] * arena_index + ["ENTER"])  # into the list, step, out again
        if mode_steps:
            groups.append(["DOWN", "DOWN"] + ["RIGHT"] * mode_steps)  # Game Type, Game Mode: the custom mode
            groups.append(["DOWN", "DOWN"])  # Max Players, Begin Game (Kills / Time / Teams are fixed by the mode)
        else:
            groups.append(
                ["DOWN"] * 6
            )  # Game Mode, Max Players, Kills, Time, Teams, Begin Game (Game Type is greyed in the Versus scope: focus skips it)
    return groups


def taps(start: int, keys: list[str], gap: int = 250) -> tuple[list[str], int]:
    """Key tap lines for `keys` from `start` ms, `gap` ms apart; returns the lines and the time after the last tap."""
    lines = []
    t = start
    for k in keys:
        lines.append(f"{t} key tap {k}")
        t += gap
    return lines, t


def screens_recipe() -> str:
    """One shot per page and state. Multiplayer page rows: Player Name, Color, Human Skin, Dragon Skin, Speed, Host,
    Join, mplayer.com (greyed), Back. The flow: Menu, Host (arena list, the list entered with Select, teams, Co-op),
    Join (list, Add Server), Host again through Begin Game to the lobby, Start Match (the countdown card)."""
    lines = ["clock milliseconds", "0 sync screen 8 3", "1500 shot title_main"]
    t = 2000
    for keys, shot in (
        (["DOWN"], "title_multiplayer_focus"),
        (["ENTER"], "mp_menu"),
        (["DOWN", "RIGHT", "RIGHT"], "mp_menu_colour"),  # Player Color
        (["DOWN"] * 4 + ["ENTER"], "mp_host"),  # Host Game
        (["DOWN"] * 3, "mp_host_map_row"),  # the Map row
        (["ENTER"], "mp_host_list_mode"),  # Select enters the list: Up / Down step through the maps
        (["DOWN"], "mp_host_map2"),
        (["DOWN", "ENTER"], None),  # third map, back out of the list
        (["DOWN"] * 6 + ["RIGHT"], "mp_host_teams"),  # Number of Teams: 2 = Team Deathmatch
        (["UP"] * 5 + ["RIGHT"], "mp_host_coop"),  # Game Type: Co-op (campaign levels)
        (["RIGHT"], None),  # Game Type back to Team Deathmatch (teams stay 2): the lobby is a team lobby
        (["ESC"], None),
        (["DOWN"] * 6 + ["ENTER"], "mp_join_lan"),  # Join Game
        (["DOWN", "DOWN", "ENTER"], "mp_join_add_server"),  # Refresh -> Add Server (entry open)
        (["ESC", "ESC"], None),
        (["DOWN"] * 5 + ["ENTER"], None),  # Host Game again
        (["DOWN"] * 3 + ["DOWN"] * 7 + ["ENTER"], "mp_lobby"),  # Begin Game: the lobby, alone
        (["DOWN", "ENTER"], "mp_lobby_countdown"),  # Start Match: the countdown card
    ):
        ls, t = taps(t, keys)
        lines += ls
        if shot:
            lines.append(f"{t + 400} shot {shot}")
            t += 900
    lines.append(f"{t + 1500} shot mp_lobby_countdown_2")
    lines.append(f"{t + 2500} quit")
    return "\n".join(lines) + "\n"


def recipe_a_text(
    level_index: int, walk: str, quit_at: int, coop: bool = False, arena_index: int = 0, kick: bool = False
) -> str:
    """A: Multiplayer > Host Game > (deathmatch: the map list, arena `arena_index`; co-op: Game Type Co-op and campaign
    level `level_index`) > Begin Game; waits in the lobby for B, then Start Match (a 3 s countdown) or, with `kick`,
    Kick player (the lobby rows are Ready, Start Match, Kick player, Leave)."""
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 1500
    for keys in host_keys(arena_index, coop, level_index):
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 500
    lines += [f"{t} shot a_host_form", f"{t + 300} key tap ENTER", f"{t + 2500} shot a_lobby_alone"]
    lines += ["22000 shot a_lobby_with_b"]
    if kick:
        lines += ["22500 key tap DOWN", "22800 key tap DOWN", "23100 shot a_lobby_kick_row", "23400 key tap ENTER"]
        lines += ["26000 shot a_lobby_after_kick", "28000 quit"]
        return "\n".join(lines) + "\n"
    lines += ["22500 key tap DOWN", "23000 key tap ENTER", "23500 shot a_lobby_countdown", "0 sync play 60", walk]
    lines.append(f"{quit_at} quit")
    return "\n".join(lines) + "\n"


def recipe_b_text(shots: str, quit_at: int, coop: bool = False, kick: bool = False) -> str:
    """B: Multiplayer > Join Game; the table's last row (UP from Refresh) is the server; waits in the lobby. Co-op: B
    picks a character in the lobby (UP from Ready to the Character row, RIGHT cycles it). `kick`: B stays in the lobby
    until A removes it and shows the Join page with the reason."""
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    t = 3000
    for keys in (["DOWN", "ENTER"], ["DOWN"] * 6 + ["ENTER"]):
        ls, t = taps(t, keys, 200)
        lines += ls
        t += 600
    lines += [f"{t + 2000} shot b_join_list", f"{t + 2300} key tap UP", f"{t + 2700} shot b_join_selected"]
    lines += [f"{t + 3000} key tap ENTER", f"{t + 5200} shot b_lobby"]
    if coop:
        lines += [f"{t + 5600} key tap UP", f"{t + 6000} key tap RIGHT", f"{t + 6500} shot b_lobby_character"]
    if kick:
        lines += [f"{t + 24000} shot b_after_kick", f"{t + 26000} quit"]
        return "\n".join(lines) + "\n"
    lines += ["0 sync play 60", shots, f"{quit_at} quit"]
    return "\n".join(lines) + "\n"


def pvp_recipe(role: str, arena_index: int = 0, mode_steps: int = 0, start_ms: int = 22000) -> str:
    """The whole flow for one instance. Times are ms after the preceding `sync`.

    A hosts (Multiplayer > Host Game > Begin Game, Start Match in the lobby), B joins from the LAN list. In the level both
    equip the dagger (inventory cell hover + ENTER, as in the pvp runs); A swings every 0.9 s, B every 2.7 s, so A scores
    the 3 kills. After the results both leave to the title (Esc, Quit, Quit to Title), the lobby page is back there,
    A starts the next match."""
    a = role == "a"
    lines = ["clock milliseconds", "0 sync screen 8 3"]
    if a:
        t = 1500
        for keys in host_keys(arena_index, mode_steps=mode_steps):
            ls, t = taps(t, keys, 200)
            lines += ls
            t += 500
        lines += [f"{t} shot a_host_form", f"{t + 300} key tap ENTER", f"{t + 2500} shot a_lobby_alone"]
        lines += [
            f"{start_ms} shot a_lobby_with_b",
            f"{start_ms + 500} key tap DOWN",
            f"{start_ms + 1000} key tap ENTER",
        ]
    else:
        t = 3000
        for keys in (["DOWN", "ENTER"], ["DOWN"] * 6 + ["ENTER"]):
            ls, t = taps(t, keys, 200)
            lines += ls
            t += 600
        lines += [f"{t + 2000} shot b_join_list", f"{t + 2300} key tap UP", f"{t + 2700} shot b_join_selected"]
        lines += [f"{t + 3000} key tap ENTER", f"{t + 5200} shot b_lobby"]
    me = "a" if a else "b"
    lines += ["0 sync play 60", f"2500 shot {me}_m1_start", f"6500 shot {me}_m1_chat_a", f"9500 shot {me}_m1_chat_b"]
    lines += [
        "12000 key down TAB",
        "12120 key up TAB",
        "0 sync screen 21 20",
        "800 cursor 216 124",  # the modern inventory: the dagger is the first cell (1280x720 pixel 432,175)
        "900 cursor 217 125",
        "2600 key down ENTER",  # the first inventory visit raises a native help box: this ENTER closes it ...
        "2900 key up ENTER",
        "4000 key down ENTER",  # ... this one equips the dagger (Equip Right Hand)
        "4300 key up ENTER",
        f"5500 shot {me}_equipped",
        "6300 key down TAB",
        "6500 key up TAB",
        "0 sync play 60",
    ]
    step = 900 if a else 2700
    n = 90 if a else 30
    timed: list[tuple[int, str]] = []  # the fight, merged by time (events must be in ascending order)
    for i in range(n):
        t = 1500 + step * (i + 1)
        timed += [(t, "button down 1"), (t + 60, "button up 1")]
        if (i % 4 == 1) if a else (i % 2 == 1):
            timed.append((t + 450, f"shot {me}_m1_{i:02d}"))
    timed.sort(key=lambda x: x[0])
    lines += [f"{t} {text}" for t, text in timed]
    t = timed[-1][0]
    # Back to the title: Esc, Quit (4th row), Quit to Title (first row of the confirm page).
    lines += [f"{t + 1500} key tap ESC", f"{t + 2500} shot {me}_pause"]
    ls, t = taps(t + 3000, ["DOWN"] * 4 + ["ENTER"], 300)
    lines += ls
    ls, t = taps(t + 800, ["ENTER"], 300)
    lines += ls
    lines += ["0 sync screen 8 3", f"2500 shot {me}_lobby_again"]
    if a:
        lines += ["6000 key tap DOWN", "6500 key tap ENTER"]  # Start Match, second round
    lines += ["0 sync play 60", f"3000 shot {me}_m2_start", f"9000 shot {me}_m2_hud", "24000 quit"]
    return "\n".join(lines) + "\n"


def pvp_input_thread(procs: dict[str, subprocess.Popen], outs: dict[str, Path], notes: list[str]) -> None:
    """Real keyboard input for what the input script cannot do; timing from the games' logs (first Playing phase)."""
    import winkeys

    def wait_for(pattern: str, who: str, n: int = 1, timeout: float = 240.0) -> bool:
        end = time.time() + timeout
        while time.time() < end:
            if len(re.findall(pattern, text_of(outs[who]))) >= n:
                return True
            if procs[who].poll() is not None:
                return False
            time.sleep(0.5)
        return False

    try:
        if not wait_for(r"phase -> 2", "b"):
            notes.append("input thread: no Playing phase seen")
            return
        t0 = time.time()
        hwnd = {k: winkeys.find_window(p.pid) for k, p in procs.items()}

        def at(sec: float) -> None:
            time.sleep(max(0.0, t0 + sec - time.time()))

        def say(who: str, text: str, tries: int = 3) -> None:
            other = "b" if who == "a" else "a"
            for attempt in range(1, tries + 1):
                ok = winkeys.focus(hwnd[who])
                winkeys.tap(winkeys.VK_F6)  # chat opens (and the game's own input is suspended)
                time.sleep(0.5)
                winkeys.type_text(text)
                time.sleep(0.3)
                winkeys.tap(winkeys.VK_RETURN)
                # The keyboard is shared with whoever else uses this PC: check the message arrived, else try again.
                end = time.time() + 4.0
                while time.time() < end and f"<{'HostRynn' if who == 'a' else 'JoinRynn'}> {text}" not in text_of(
                    outs[other]
                ):
                    time.sleep(0.25)
                arrived = f"<{'HostRynn' if who == 'a' else 'JoinRynn'}> {text}" in text_of(outs[other])
                notes.append(f"input: {who} typed {text!r}, try {attempt} (window focused: {ok}, arrived: {arrived})")
                if arrived:
                    return

        at(4.0)
        say("b", "gg from JoinRynn")
        at(7.5)
        say("a", "hello back HostRynn")

        def show(who: str, name: str, hold_f4: float = 0.0) -> None:
            """Focus the window, optionally hold F4, and capture the screen under it (the window is on top)."""
            winkeys.focus(hwnd[who])
            if hold_f4:
                winkeys.key_down(winkeys.VK_F4)
            time.sleep(0.8)
            ok = winkeys.capture(hwnd[who], outs[who] / f"cap_{name}.png")
            if hold_f4:
                time.sleep(max(0.0, hold_f4 - 0.8))
                winkeys.key_up(winkeys.VK_F4)
            notes.append(f"capture: {who} {name} ({'ok' if ok else 'failed'})")

        # The first kill: B's death overlay (countdown), then the scoreboard held on A and on B.
        if wait_for(r"death: player 2 killed by player 1", "a"):
            show("b", "b_death_overlay_1")
            show("a", "a_f4_scoreboard", 2.5)
            show("b", "b_f4_scoreboard", 2.5)
        # The end of the match: the results banner with the final standings on both sides.
        if wait_for(r"match over \(", "a"):
            time.sleep(1.0)
            show("a", "a_results")
            show("b", "b_results")
        # Second match: the scoreboard again with zero scores, 7 s after Playing began.
        if wait_for(r"phase -> 2", "b", n=2):
            time.sleep(7.0)
            show("a", "a_m2_f4_scoreboard", 2.5)
            show("b", "b_m2_f4_scoreboard", 2.5)
    except Exception as e:  # keep the harness alive: the checks report what is missing
        notes.append(f"input thread failed: {e!r}")


def pvp_flow_checks(ta: str, tb: str, n: int, notes: list[str], by_time: bool = False) -> dict[str, bool]:
    """The flow's checks from the two logs: menu, joining, the match, the HUD, results, the lobby again, match two."""
    c: dict[str, bool] = {}
    c["A hosted through the menu, B joined from the LAN list"] = (
        "multiplayer action HostMatch" in ta and "multiplayer action JoinMatch" in tb
    )
    c["both launched the match level (New Game)"] = (
        "multiplayer launch: New Game with" in ta and "multiplayer launch: New Game with" in tb
    )
    c["phase Playing on both"] = "phase -> 2" in ta and "phase -> 2" in tb
    if not by_time:  # a match that ends by time may well have no kill (the time run only checks the end)
        c["A claims hits, the server accepts them"] = (
            len(re.findall(r"claim: hit on player 2", ta)) >= 4
            and len(re.findall(r"\] hit: player 1 hit player 2", ta)) >= 4
        )
        c["kills are scored (death + score lines)"] = len(re.findall(r"death: player 2 killed by player 1", ta)) >= max(
            n, 1
        )
        c["B respawns (frozen death, then a spawn point)"] = (
            "[pvp] killed by player 1" in tb and re.search(r"\[pvp\] respawned at spawn", tb) is not None
        )
    reason, what = ("time limit", "the time limit") if by_time else ("frag limit", f"{n} kills (frag limit)")
    c[f"match over at {what} on both"] = all(re.search(rf"match over \({reason}\)", t) is not None for t in (ta, tb))
    c["chat B -> A and A -> B delivered (typed with SendInput)"] = (
        "<JoinRynn> gg from JoinRynn" in ta
        and "<HostRynn> hello back HostRynn" in tb
        and not any("failed" in x for x in notes)
    )
    c["both returned to the title with the lobby resumed"] = ta.count("title: resumed the multiplayer lobby") >= 1 and (
        tb.count("title: resumed the multiplayer lobby") >= 1
    )
    c["second match started (Playing twice, scores reset)"] = len(re.findall(r"phase -> 2", ta)) >= 2 and (
        len(re.findall(r"phase -> 2", tb)) >= 2
    )
    return c


WINDOW = [1280, 720]  # game window client size (--size)
PORT: list[int] = []  # --port: the base UDP port of the rig (DRAKAN_MP_PORT: servers and the LAN sweep), none = 47800


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
            "DRAKAN_WINDOW_WIDTH": str(WINDOW[0]),
            "DRAKAN_WINDOW_HEIGHT": str(WINDOW[1]),
        }
    )
    if PORT:
        env["DRAKAN_MP_PORT"] = str(PORT[0])
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
        "--pvp", action="store_true", help="menu -> deathmatch -> results -> next match (see the docstring)"
    )
    ap.add_argument("--frag-limit", type=int, default=3, help="--pvp: the host's kill limit (test aid env override)")
    ap.add_argument(
        "--time-limit-sec",
        type=int,
        default=0,
        help="--pvp: the host's time limit in seconds (test aid env override); with --frag-limit 0 the match ends by time",
    )
    ap.add_argument("--coop", action="store_true", help="host a Co-op match from the menu (Game Type: Co-op)")
    ap.add_argument(
        "--prefix", default=None, help="private play folders build/<prefix>-a and -b (default mp-ui / mp-full)"
    )
    ap.add_argument(
        "--level-index", type=int, default=0, help="map row position (0 = Surdana; others use the file redirect)"
    )
    ap.add_argument(
        "--map",
        default=None,
        help="--pvp: host this multiplayer map (configs/mp_maps.ini id or name; DRAKAN_MP_MAP test aid: the menu's own map "
        "row decides without it) and check the arena world mode (docs/reference/netcode.md 'Multiplayer maps')",
    )
    ap.add_argument(
        "--arena-index",
        type=int,
        default=0,
        help="deathmatch: the position of the map in the Host page's map list (the menu's own list row picks it, "
        "ARENAS above); the checks expect the arena's base level",
    )
    ap.add_argument(
        "--kick", action="store_true", help="A removes B from the lobby with Kick player (see recipe_a_text)"
    )
    ap.add_argument("--size", default="1280x720", help="window client size WxH, e.g. 1920x1080 or 3840x2160")
    ap.add_argument(
        "--play-sec", type=int, default=0, help="seconds in the match before quitting (default 16 / 30 co-op)"
    )
    ap.add_argument(
        "--port", type=int, default=0, help="base UDP port of the rig (default 47800; other rigs use others)"
    )
    ap.add_argument(
        "--mode-steps",
        type=int,
        default=0,
        help="--pvp: Right presses on the Host page's Game Mode row (1 = the first custom mode, instagib-lite)",
    )
    a = ap.parse_args()
    WINDOW[:] = [int(v) for v in a.size.lower().split("x")]
    PORT[:] = [a.port] if a.port else []
    name = a.name or (
        "mp_menu_screens" if a.screens else "mp_full" if a.pvp else "mp_menu_coop" if a.coop else "mp_menu_two"
    )
    run = ROOT / "build" / "harness" / name
    pre = a.prefix or ("mp-full" if a.pvp else "mp-ui")
    folders = {"a": ROOT / "build" / f"{pre}-a", "b": ROOT / "build" / f"{pre}-b"}
    exes = {k: prepare_folder(SRC, a.exe, v) for k, v in folders.items()}
    for k in ("a", "b"):
        (run / k).mkdir(parents=True, exist_ok=True)
        for stale in [*(run / k).glob("*.png"), *(run / k).glob("*.bmp")]:
            stale.unlink()  # screenshots of an earlier run must not pass for this one
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
        ok = len(shots) >= 14
    elif a.pvp:
        timeout = max(a.timeout, 330)
        for k in ("a", "b"):
            (run / k / "recipe.txt").write_text(
                pvp_recipe(k, a.arena_index, a.mode_steps), encoding="utf-8", newline="\n"
            )
        aids = {
            "DRAKAN_PVP_DAMAGE_SCALE": "8",
            "DRAKAN_PVP_TEST_PLACE": "140",
            "DRAKAN_PVP_TEST_ANCHOR": "1",
            "DRAKAN_NO_ACTIVATE": "0",  # the windows take the focus: SendInput goes to the foreground window
            "DRAKAN_NET_CHAT_DEBUG": "1",
        }
        env_a = game_env(run / "a", folders["a"], run / "a" / "recipe.txt", timeout, "HostRynn")
        env_a.update(aids)
        env_a["DRAKAN_MP_FRAG_LIMIT"] = str(a.frag_limit)
        if a.time_limit_sec:
            env_a["DRAKAN_MP_TIME_LIMIT_SEC"] = str(a.time_limit_sec)
        env_b = game_env(run / "b", folders["b"], run / "b" / "recipe.txt", timeout, "JoinRynn")
        env_b.update(aids)
        if a.map:
            for e in (env_a, env_b):
                e["DRAKAN_ARENA_DUMP"] = "1"  # arena_before / arena_after world dumps in <folder>/dev/world
                e.pop("DRAKAN_PVP_TEST_ANCHOR", None)  # the players stand on their spawn points
                e["DRAKAN_PVP_MAX_HIT_RANGE"] = "1000000"  # ... far apart: the server's reach check is off for the run
            env_a["DRAKAN_MP_MAP"] = a.map
        pa = start(exes["a"], env_a, run / "a")
        time.sleep(6)
        pb = start(exes["b"], env_b, run / "b")
        notes: list[str] = []
        th = threading.Thread(
            target=pvp_input_thread,
            args=({"a": pa, "b": pb}, {"a": run / "a", "b": run / "b"}, notes),
            daemon=True,
        )
        th.start()
        deadline = time.time() + timeout + 90
        for p in (pa, pb):
            try:
                p.wait(timeout=max(1, deadline - time.time()))
            except subprocess.TimeoutExpired:
                p.kill()
        th.join(timeout=5)
        ta, tb = text_of(run / "a"), text_of(run / "b")
        for n in notes:
            log(n)
        checks = pvp_flow_checks(ta, tb, a.frag_limit, notes, bool(a.time_limit_sec) and not a.frag_limit)
        for k, v in checks.items():
            log(f"{'ok  ' if v else 'FAIL'} {k}")
        summary["checks"] = checks
        summary["notes"] = notes
        ok = all(checks.values())
    else:
        walk = "500 key down W\n3000 key up W"
        level = LEVELS[a.level_index] if a.coop else ARENAS[a.arena_index][1]
        shots_a = "\n6000 shot a_in_level\n9000 shot a_sees_b\n12500 shot a_scoreboard"
        recipe_a = recipe_a_text(
            a.level_index,
            walk + shots_a,
            a.play_sec * 1000 or (30000 if a.coop else 16000),
            a.coop,
            a.arena_index,
            a.kick,
        )
        recipe_b = recipe_b_text(
            "1000 shot b_in_level\n5000 shot b_sees_a",
            a.play_sec * 1000 + 1000 if a.play_sec else (31000 if a.coop else 17000),
            a.coop,
            a.kick,
        )
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
        if a.coop:  # a Coop match: the creature stream rides the session (protocol v6)
            checks["A runs co-op as the host"] = "co-op on: host" in ta
            checks["B runs co-op as a client"] = "co-op on: client" in tb
            checks["B's creatures follow the host's"] = any(
                int(m) > 0 for m in re.findall(r"client stats: \d+ packets, keyed \d+, matched (\d+)", tb)
            )
            checks["the server is in co-op mode"] = "co-op authority: player 1" in ta
        if not a.coop:  # the menu's map list picked an arena: the server hosts it and both load its base level
            checks[f"A hosted the arena '{ARENAS[a.arena_index][0]}' from the map list"] = (
                re.search(rf"host: listening on UDP \d+ .*level '{re.escape(ARENAS[a.arena_index][0])}'", ta)
                is not None
            )
            if not a.kick:
                checks["the arena world mode is on in both"] = (
                    "arena world mode on" in ta and "arena world mode on" in tb
                )
        else:  # B picked a character in the lobby: the screen announced it, the session carries it
            checks["B chose a co-op character in the lobby"] = bool(re.search(r"co-op character [1-9]\d* chosen", tb))
        if a.kick:
            checks = {k: v for k, v in checks.items() if not k.startswith(("A launched", "B launched", "A loaded"))}
            checks = {k: v for k, v in checks.items() if not k.startswith(("B loaded", "A spawned", "B spawned"))}
            checks["A kicked B (KickPlayer action)"] = "kick player" in ta
            checks["B was rejected as kicked"] = "Kicked by the host" in tb and "REJECTED" in tb
            checks["B returned to the Join page and left the session"] = "multiplayer action Leave" in tb
        else:
            checks["the lobby showed a start countdown (server Status)"] = "start countdown" in ta
        for k, v in checks.items():
            log(f"{'ok  ' if v else 'FAIL'} {k}")
        summary["checks"] = checks
        ok = all(checks.values())
    (run / "summary.json").write_text(json.dumps(summary, indent=1), encoding="utf-8", newline="\n")
    log(f"output: {run}")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
