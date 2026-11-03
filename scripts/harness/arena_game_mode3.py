"""A custom game mode played by two or three instances: last one standing with three players, named teams, allowed
weapons, the mode loadout and a leaver (docs/reference/netcode.md "Custom game modes").

    python scripts/harness/arena_game_mode3.py --mods build/scratch/gamemodes2/mods --map inquisitor-gm \
        --mode last-rynn-standing --scenario lrs
    python scripts/harness/arena_game_mode3.py ... --mode claws-vs-wings --scenario teams
    python scripts/harness/arena_game_mode3.py ... --mode steel-club-only --scenario weapons
    python scripts/harness/arena_game_mode3.py ... --mode claws-vs-wings --scenario lobby --lobby-map 3

Sibling of arena_game_mode.py (two instances). A hosts with the mode (DRAKAN_NET_HOST + DRAKAN_NET_GAME_MODE), and with
three players waits for all of them in the lobby (DRAKAN_NET_AUTO_START_PLAYERS: a third player joining a last-standing
match in progress would only watch). A swings at one opponent at a time (DRAKAN_PVP_TEST_ONE: the ghost of one player
still in the match is held in front of A, the target moves on every few seconds, the others float out of reach).

Scenarios (checks read the logs; screenshots under build/harness/<name>/{a,b,c}/):
  lrs      three players, lives: B and C lose lives in turn, the first one out watches ("You are out") while the other
           two play on, the match ends last one standing with A the winner, the standings show OUT.
  teams    three players, `base = tdm` with team names / colours: A (team 0) and C (team 0) against B (team 1); A hits B
           (score_kill) and C (friendly fire, score_team_kill); scoreboard and results with the team names; the mode's
           allowed weapon scores; the mode loadout reaches every inventory (C opens it).
  weapons  two players: the mode allows a weapon nobody holds, so A's hits are refused; then B quits and A's client
           reports the right end reason (last one standing, not the time limit).
  lobby    the title menu: A hosts the mode from the Host page and waits in the lobby, B and C join it with the
           DRAKAN_NET_CONNECT shortcut; screenshots of the lobby's team columns.
Private play folders build/<prefix>-{a,b,c}; delete them with `cmd /c rmdir /s /q` (they hold junctions).
"""

from __future__ import annotations

import argparse
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from arena_game_mode import WINDOW, map_file
from arena_tour import find_map
from netplay_two_instances import game_env, prepare_folder

ROOT = Path(__file__).resolve().parents[2]


def log(msg: str) -> None:
    print(f"[game-mode3] {msg}", flush=True)


def recipe(role: str, scenario: str, fight_s: int, players: int) -> str:
    """Times are ms after each instance's `sync play`."""
    lines = ["clock milliseconds", f"0 sync play {60 * players}", f"2000 shot {role}_spawn", f"9000 shot {role}_arena"]
    end = 24000 + fight_s * 1000
    if role == "a":
        t, i = 24000, 0
        while t < end:
            lines += [f"{t} button down 1", f"{t + 60} button up 1"]
            if i % 8 == 1:
                lines.append(f"{t + 450} shot a_fight_{i:02d}")
            t += 900
            i += 1
    if role in ("a", "c"):
        for at in range(34000, end, 20000):  # the scoreboard (team names, LIVES / OUT)
            lines += [f"{at} key down F4", f"{at + 400} shot {role}_scoreboard_{at // 1000}", f"{at + 700} key up F4"]
    if role == "c" or (role == "b" and scenario == "weapons"):
        # The inventory with the mode's loadout (the arena gives it some 20 s after the level pass).
        # The first open shows the Equipment help: OK it, shoot the grid, close with Esc.
        lines += [
            "19000 key tap TAB",
            f"20200 shot {role}_inventory_help",
            "20500 key tap ENTER",
            f"21500 shot {role}_inventory",
            "22200 key tap ESC",
            f"23200 shot {role}_inventory_closed",
        ]
    t, i = 24000, 0
    while t < end + 30000:
        lines.append(f"{t} shot {role}_s{i:02d}")
        t += 3000
        i += 1
    if role == "b" and scenario == "weapons":
        lines.append(f"{24000 + 45000} quit")  # the last opponent leaves mid-match
    else:
        lines.append(f"{t + 4000} quit")
    return "\n".join(lines[:2] + sorted(lines[2:], key=lambda s: int(s.split()[0]))) + "\n"


def lobby_recipe(map_index: int, mode_steps: int) -> str:
    """Title > Multiplayer > Host Game, the map list stepped to `map_index`, the Game Mode row stepped `mode_steps`
    times, Begin Game: the lobby, shot while the others join."""
    lines = ["clock milliseconds", "0 sync screen 8 3", "1500 shot lobby_title"]
    t = 2000
    for keys, shot in (
        (["DOWN", "ENTER"], "lobby_mp_menu"),
        (["DOWN"] * 5 + ["ENTER"], "lobby_form"),
        (["DOWN"] * 3 + ["ENTER"] + ["DOWN"] * map_index + ["ENTER"], "lobby_map"),
        (["DOWN"], "lobby_mode_0"),
        *[(["RIGHT"], f"lobby_mode_{k + 1}") for k in range(mode_steps)],
        (["DOWN", "DOWN", "ENTER"], "lobby_begin"),  # Maximum Number of Players, Begin Game (the greyed rows skipped)
    ):
        for k in keys:
            lines.append(f"{t} key tap {k}")
            t += 250
        lines.append(f"{t + 600} shot {shot}")
        t += 1400
    for k in range(8):
        lines.append(f"{t + k * 5000} shot lobby_{k}")
    lines.append(f"{t + 45000} quit")
    return "\n".join(lines) + "\n"


def run(
    procs_env: dict[str, tuple[Path, dict[str, str]]], out: Path, timeout: int, stagger: float, wait_for: str = ""
) -> None:
    """Starts the instances `stagger` s apart; with `wait_for`, the others only once A's log has that text."""
    procs: dict[str, subprocess.Popen] = {}
    t0 = time.monotonic()
    try:
        for k, (exe, env) in procs_env.items():
            if wait_for and k != "a":
                alog = out / "a" / "stdout_1.log"
                while time.monotonic() - t0 < timeout and wait_for not in alog.read_bytes().decode("latin-1"):
                    time.sleep(1)
            procs[k] = subprocess.Popen(
                [str(exe)],
                cwd=exe.parent,
                env=env,
                stdout=(out / k / "stdout_1.log").open("wb"),
                stderr=subprocess.STDOUT,
            )
            log(f"started {k.upper()} (pid {procs[k].pid})")
            time.sleep(stagger)
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > timeout + 90:
                log("timeout: stopping the instances")
                break
            time.sleep(1)
    finally:
        for k, p in procs.items():
            if p.poll() is None:
                log(f"stopping {k.upper()} (pid {p.pid})")
                p.kill()


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mods", type=Path, required=True)
    ap.add_argument("--map", required=True)
    ap.add_argument("--mode", required=True)
    ap.add_argument("--scenario", choices=("lrs", "teams", "weapons", "lobby"), required=True)
    ap.add_argument("--name", default="")
    ap.add_argument("--prefix", default="game-mode3")
    ap.add_argument("--port", type=int, default=47974)
    ap.add_argument("--fight", type=int, default=0, help="seconds A swings (default by scenario)")
    ap.add_argument("--one", type=int, default=420, help="DRAKAN_PVP_TEST_ONE: boundaries per target")
    ap.add_argument("--timeout", type=int, default=0)
    ap.add_argument("--lobby-map", type=int, default=3, help="lobby: the Host page's map list index")
    ap.add_argument("--lobby-mode", type=int, default=3, help="lobby: RIGHT taps on the Game Mode row")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--env", action="append", default=[], metavar="K=V")
    a = ap.parse_args()

    sc = a.scenario
    players = 2 if sc == "weapons" else 3
    roles = "abc"[:players]
    fight = a.fight or {"lrs": 110, "teams": 150, "weapons": 60, "lobby": 0}[sc]
    timeout = a.timeout or (24 + fight + 80)
    mods = a.mods.resolve()
    m = find_map(a.map, map_file(mods, a.map))
    name = a.name or f"game_mode3_{sc}"
    out = ROOT / "build" / "harness" / name
    if out.exists():
        shutil.rmtree(out)
    for k in roles:
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-{k}") for k in roles}
    window = {
        "DRAKAN_WINDOW_WIDTH": str(WINDOW[0]),
        "DRAKAN_WINDOW_HEIGHT": str(WINDOW[1]),
        "DRAKAN_WS_ASPECT": "16:9",
    }
    if sc == "lobby":
        port = 47800 + (a.port % 10)
        envs: dict[str, tuple[Path, dict[str, str]]] = {}
        (out / "a" / "recipe.txt").write_text(lobby_recipe(a.lobby_map, a.lobby_mode), encoding="utf-8", newline="\n")
        env = game_env(out / "a", exes["a"].parent, out / "a" / "recipe.txt", 120, {"DRAKAN_MODS": str(mods)})
        for k in ("DRAKAN_START_LEVEL", "DRAKAN_GHOST", "DRAKAN_AUTOPRESS", "DRAKAN_AUTOPRESS_UNTIL"):
            env.pop(k, None)
        env.update({**window, "DRAKAN_NET_NAME": "Alpha", "DRAKAN_MP_PORT": str(port)})
        envs["a"] = (exes["a"], env)
        for k, nm in (("b", "Bravo"), ("c", "Charlie")):
            (out / k / "recipe.txt").write_text(
                "clock milliseconds\n0 sync screen 8 3\n60000 quit\n", encoding="utf-8", newline="\n"
            )
            env = game_env(out / k, exes[k].parent, out / k / "recipe.txt", 100, {"DRAKAN_MODS": str(mods)})
            for v in ("DRAKAN_START_LEVEL", "DRAKAN_GHOST", "DRAKAN_AUTOPRESS", "DRAKAN_AUTOPRESS_UNTIL"):
                env.pop(v, None)
            env.update({**window, "DRAKAN_NET_NAME": nm, "DRAKAN_NET_CONNECT": f"127.0.0.1:{port}"})
            envs[k] = (exes[k], env)
        # A needs 20 .. 45 s to reach the lobby: the clients start once it listens (a refused first try is not retried).
        run(envs, out, 150, 3, wait_for="host: listening")
        ta = (out / "a" / "stdout_1.log").read_bytes().decode("latin-1")
        print("\n".join(line for line in ta.splitlines() if "server:" in line or "host" in line.lower())[:4000])
        print(out)
        return 0

    for k in roles:
        (out / k / "recipe.txt").write_text(recipe(k, sc, fight, players), encoding="utf-8", newline="\n")
    common = {
        "DRAKAN_MODS": str(mods),
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_MP_MAP": m["id"],
        "DRAKAN_PVP_MAX_HIT_RANGE": "1000000",
        "DRAKAN_PVP_DAMAGE_SCALE": "8",
        "DRAKAN_MP_RESULTS_SEC": "40",
        **window,
    }
    for kv in a.env:
        common[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    names = {"a": "Alpha", "b": "Bravo", "c": "Charlie"}
    extra: dict[str, dict[str, str]] = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_NET_GAME_MODE": a.mode,
            "DRAKAN_NET_AUTO_START_PLAYERS": str(players),
            "DRAKAN_PVP_TEST_PLACE": "140",
            "DRAKAN_PVP_TEST_ONE": str(a.one),
        }
    }
    for k in roles[1:]:
        extra[k] = {**common, "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": names[k]}
    if sc == "teams":  # scripted input cannot hold F4: C shows the scoreboard from 70 s into the match on
        extra["c"]["DRAKAN_NET_SCOREBOARD"] = "70"
    envs = {}
    for k in roles:
        env = game_env(out / k, exes[k].parent, out / k / "recipe.txt", timeout, extra[k])
        env.pop("DRAKAN_GHOST", None)
        envs[k] = (exes[k], env)
    run(envs, out, timeout, 4)

    text = {k: (out / k / "stdout_1.log").read_bytes().decode("latin-1") for k in roles}
    ta = text["a"]
    checks: dict[str, bool] = {}
    for k in roles:
        checks[f"{k}: arena world mode on for '{m['name']}'"] = f"arena world mode on: {m['name']}" in text[k]
        checks[f"{k}: no [modes] errors"] = "[modes]" not in text[k]
    checks[f"A hosts mode '{a.mode}'"] = f"hosting with mode '{a.mode}'" in ta
    if players == 3:
        checks["the server waits for three players (lobby countdown)"] = "server: lobby countdown started" in ta
    if sc == "lrs":
        elim = {p: ta.find(f"server: player {p} eliminated (out of lives)") for p in (2, 3)}
        checks["B and C each lose lives (server)"] = all(
            re.search(rf"server: player {p} has [12] li(ves|fe) left", ta) for p in (2, 3)
        )
        checks["both B and C are eliminated (server)"] = all(v >= 0 for v in elim.values())
        first, second = sorted(elim, key=lambda p: elim[p]) if all(v >= 0 for v in elim.values()) else (0, 0)
        # Between the first and the second elimination the other two play on (A swings, the second one is still in).
        between = ta[elim.get(first, 0) : elim.get(second, 0)] if first else ""
        stamp = {
            p: float(x.group(1))
            for p in (2, 3)
            if (x := re.search(rf"\[netplay ([\d.]+)\] server: player {p} eliminated", ta))
        }
        gap = stamp[second] - stamp[first] if first and len(stamp) == 2 else 0.0
        checks[f"after the first elimination the other two play on ({gap:.1f} s until the second)"] = gap >= 4.0
        checks["no phase change between the eliminations"] = bool(first) and "phase -> results" not in between
        over = ta.find("server: phase -> results")
        checks["the first one out is not respawned"] = (
            bool(first) and re.search(rf"respawn: player {first} at spawn", ta[elim[first] : over]) is None
        )
        checks["the server ends it: last one standing, A wins"] = (
            "server: last one standing" in ta and "server: match over: player 1 wins" in ta
        )
        for k in roles:
            checks[f"{k}: match over (last one standing): player 1 wins"] = (
                re.search(r"match over \(last one standing\): player 1 wins", text[k]) is not None
            )
        for k in roles:
            checks[f"{k}: knows player {first} is out (Score lives 0)"] = bool(first) and (
                re.search(rf"score: player {first} .* lives 0 \(eliminated\)", text[k]) is not None
            )
    elif sc == "teams":
        checks["A kills B (enemy) on the server"] = "server: player 2 killed by 1" in ta
        checks["A kills C (team mate, friendly fire on)"] = "server: player 3 killed by 1" in ta
        checks["no hit refused as friendly fire"] = "hit rejected: friendly fire" not in ta
        checks["no hit refused for the weapon (the held Iron Club is allowed)"] = "weapon not allowed" not in ta
        # Points: score_kill 3 for an enemy kill, score_team_kill -2 for a team kill.
        first_match = ta[: ta.find("server: phase -> results")]  # the harness lets a second match start after it
        rows = re.findall(
            r"score: player 1 kills (\d+) deaths (\d+) points (-?\d+) \(team kills \d+ / \d+, team points (-?\d+) / (-?\d+)",
            first_match,
        )
        last = rows[-1] if rows else None
        nb = len(re.findall(r"server: player 2 killed by 1", first_match))
        nc = len(re.findall(r"server: player 3 killed by 1", first_match))
        checks[f"A's points = 3 x enemy kills - 2 x team kills ({nb} x 3 - {nc} x 2 = {3 * nb - 2 * nc})"] = (
            last is not None and int(last[2]) == 3 * nb - 2 * nc
        )
        checks["team points: Red Claws (A, C) = A's points, Blue Wings (B) = 0"] = (
            last is not None and int(last[3]) == int(last[2]) and int(last[4]) == 0
        )
        for k in roles:
            checks[f"{k}: match over (time limit): team 0 wins"] = (
                re.search(r"match over \(time limit\): team 0 wins", text[k]) is not None
            )
    elif sc == "weapons":
        checks["A's hits are refused: weapon not allowed in this mode"] = (
            "server: hit rejected: weapon not allowed in this mode" in ta
        )
        checks["no kills (every hit refused)"] = "killed by" not in ta
        checks["B leaves mid-match"] = re.search(r"server: player 2 'Bravo' left", ta) is not None
        checks["the server ends it: last one standing"] = "server: last one standing" in ta
        checks["A: match over (last one standing): player 1 wins (not 'time limit')"] = (
            re.search(r"match over \(last one standing\): player 1 wins", ta) is not None
        )
    # The map's loadout (the Steel Club) plus the mode's own items.
    want = 1 + {"lrs": 0, "teams": 2, "weapons": 2}[sc]
    for k in roles:
        given = re.findall(r"loadout: (\d+) items given", text[k])
        checks[f"{k}: the loadout gives {want} items (map {1}, mode {want - 1})"] = (
            bool(given) and int(given[0]) == want and "is not part of this level" not in text[k]
        )
    for k in roles:
        for line in text[k].splitlines():
            if any(
                s in line
                for s in (
                    "lives",
                    "eliminated",
                    "last one standing",
                    "match over",
                    "killed by",
                    "rejected",
                    "loadout",
                    "left",
                    "team",
                )
            ):
                print(f"{k.upper()} {line.strip()}")
    (out / "summary.json").write_text(json.dumps({"checks": checks}, indent=1), encoding="utf-8")
    for k, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + k)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
