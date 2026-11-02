"""A custom game mode from a mod pack, played by two instances (docs/reference/netcode.md "Custom game modes").

    python scripts/harness/arena_game_mode.py --mods build/scratch/gamemodes/mods --map inquisitor-lrs \
        --mode last-rynn-standing --name game_mode_lrs

`--mods` is a mods folder (DRAKAN_MODS: packs plus mods.ini) whose pack defines the map and the `[mode]`; the map file
is read from the first `maps/*.ini` that has the map. A hosts with the mode (DRAKAN_NET_HOST + DRAKAN_NET_GAME_MODE, the
shortcut the Host page's Game Mode row stands for), B joins. The test aids of arena_two_instances hold B's ghost in front of
A (DRAKAN_PVP_TEST_PLACE) and switch the reach check off; A swings, takes a pickup (DRAKAN_ARENA_TEST_TAKE) and holds F4
(scoreboard) twice. The checks read the logs: the mode loaded from the pack, the lives counting down, B eliminated (no
respawn), the match ending as last one standing with A the winner on both instances, and a taken pickup that does not come
back when the mode says so. Private play folders build/<prefix>-a / -b, output build/harness/<name>/{a,b}/, summary.json.
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
from arena_tour import find_map
from netplay_two_instances import game_env, prepare_folder

ROOT = Path(__file__).resolve().parents[2]
WINDOW = (1280, 720)


def log(msg: str) -> None:
    print(f"[game-mode] {msg}", flush=True)


def recipe(role: str, fight_s: int) -> str:
    """Times are ms after each instance's `sync play`."""
    lines = ["clock milliseconds", "0 sync play 60", f"2000 shot {role}_spawn", f"9000 shot {role}_arena"]
    end = 24000 + fight_s * 1000
    if role == "a":
        lines += ["16000 shot a_hud"]
        t = 24000
        i = 0
        while t < end:
            lines += [f"{t} button down 1", f"{t + 60} button up 1"]
            if i % 6 == 1:
                lines.append(f"{t + 450} shot a_fight_{i:02d}")
            t += 900
            i += 1
        for at in (34000, 54000):  # the scoreboard with the LIVES column
            lines += [f"{at} key down F4", f"{at + 400} shot a_scoreboard_{at // 1000}", f"{at + 700} key up F4"]
    t = 24000
    i = 0
    while t < end + 30000:
        lines.append(f"{t} shot {role}_s{i:02d}")
        t += 4000
        i += 1
    lines.append(f"{t + 4000} quit")
    return "\n".join(lines[:2] + sorted(lines[2:], key=lambda s: int(s.split()[0]))) + "\n"


def host_page_recipe(map_index: int) -> str:
    """One instance: title > Multiplayer > Host Game, the map list stepped to `map_index`, the Game Mode row stepped
    through the first three custom modes that fit the map; screenshots of the form (the rules text under it) on the way."""
    lines = ["clock milliseconds", "0 sync screen 8 3", "1500 shot host_title"]
    t = 2000
    for keys, shot in (
        (["DOWN", "ENTER"], "host_mp_menu"),  # Multiplayer
        (["DOWN"] * 5 + ["ENTER"], "host_form"),  # Host Game
        (["DOWN"] * 3 + ["ENTER"] + ["DOWN"] * map_index + ["ENTER"], "host_map"),  # the map list, out again
        (["DOWN", "RIGHT"], "host_mode"),  # Game Mode (Game Type is greyed and skipped): the first custom mode
        (["RIGHT"], "host_mode_2"),  # the next one (the built-in modes come before a pack's)
        (["RIGHT"], "host_mode_3"),
    ):
        for k in keys:
            lines.append(f"{t} key tap {k}")
            t += 250
        lines.append(f"{t + 600} shot {shot}")
        t += 1400
    lines.append(f"{t + 1500} quit")
    return "\n".join(lines) + "\n"


def map_file(mods: Path, name: str) -> Path:
    for f in sorted(mods.glob("*/maps/*.ini")):
        text = f.read_text(encoding="utf-8")
        if re.search(rf"^\[arena\s+{re.escape(name)}\]", text, re.M) or f"name = {name}" in text:
            return f
    raise SystemExit(f"no pack in {mods} defines map '{name}'")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mods", type=Path, required=True, help="mods folder (DRAKAN_MODS) with the pack and mods.ini")
    ap.add_argument("--map", required=True)
    ap.add_argument("--mode", required=True, help="the [mode <id>] to host")
    ap.add_argument("--name", default="")
    ap.add_argument("--prefix", default="game-mode")
    ap.add_argument("--port", type=int, default=47970)
    ap.add_argument("--fight", type=int, default=80, help="seconds A swings")
    ap.add_argument("--pickup", type=int, default=5, help="index of the pickup A takes (DRAKAN_ARENA_TEST_TAKE)")
    ap.add_argument("--goto-ticks", type=int, default=200, help="boundaries after the level pass when A takes it")
    ap.add_argument("--timeout", type=int, default=200)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment for both")
    ap.add_argument(
        "--host-page",
        type=int,
        default=-1,
        metavar="MAP_INDEX",
        help="only the Host page: one instance, map list index",
    )
    a = ap.parse_args()

    mods = a.mods.resolve()
    mfile = map_file(mods, a.map)
    m = find_map(a.map, mfile)
    name = a.name or ("game_mode_" + a.mode)
    out = ROOT / "build" / "harness" / name
    if out.exists():
        shutil.rmtree(out)
    for k in "ab":
        (out / k).mkdir(parents=True)
    if a.host_page >= 0:  # the Host page only: the pack's mode on the Game Mode row, its rules under the form
        exe = prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-a")
        (out / "a" / "recipe.txt").write_text(host_page_recipe(a.host_page), encoding="utf-8", newline="\n")
        env = game_env(out / "a", exe.parent, out / "a" / "recipe.txt", 90, {"DRAKAN_MODS": str(mods)})
        for k in ("DRAKAN_START_LEVEL", "DRAKAN_GHOST", "DRAKAN_AUTOPRESS", "DRAKAN_AUTOPRESS_UNTIL"):
            env.pop(k, None)
        env.update(
            {"DRAKAN_WINDOW_WIDTH": str(WINDOW[0]), "DRAKAN_WINDOW_HEIGHT": str(WINDOW[1]), "DRAKAN_WS_ASPECT": "16:9"}
        )
        with (out / "a" / "stdout_1.log").open("wb") as f:
            subprocess.run([str(exe)], cwd=exe.parent, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=200)
        print(out / "a")
        return 0
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-{k}") for k in "ab"}
    for k in "ab":
        (out / k / "recipe.txt").write_text(recipe(k, a.fight), encoding="utf-8", newline="\n")
    common = {
        "DRAKAN_MODS": str(mods),
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_MP_MAP": m["id"],
        "DRAKAN_PVP_MAX_HIT_RANGE": "1000000",
        "DRAKAN_PVP_DAMAGE_SCALE": "8",
        "DRAKAN_WINDOW_WIDTH": str(WINDOW[0]),
        "DRAKAN_WINDOW_HEIGHT": str(WINDOW[1]),
        "DRAKAN_WS_ASPECT": "16:9",
        "DRAKAN_MP_RESULTS_SEC": "40",
    }
    for kv in a.env:
        common[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    env_extra = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_NET_GAME_MODE": a.mode,
            "DRAKAN_PVP_TEST_PLACE": "140",
            "DRAKAN_ARENA_TEST_TAKE": f"{a.pickup}@{a.goto_ticks}",
        },
        "b": {**common, "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": "Bravo"},
    }
    procs: dict[str, subprocess.Popen] = {}
    logs = {k: out / k / "stdout_1.log" for k in "ab"}
    t0 = time.monotonic()
    try:
        for k in "ab":
            env = game_env(out / k, exes[k].parent, out / k / "recipe.txt", a.timeout, env_extra[k])
            env.pop("DRAKAN_GHOST", None)
            procs[k] = subprocess.Popen(
                [str(exes[k])], cwd=exes[k].parent, env=env, stdout=logs[k].open("wb"), stderr=subprocess.STDOUT
            )
            log(f"started {k.upper()} (pid {procs[k].pid})")
            time.sleep(4)
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > a.timeout + 60:
                log("timeout: stopping the instances")
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") for k in "ab"}
    ta, tb = text["a"], text["b"]
    checks: dict[str, bool] = {}
    for k in "ab":
        checks[f"{k}: arena world mode on for '{m['name']}'"] = f"arena world mode on: {m['name']}" in text[k]
        checks[f"{k}: no [modes] errors"] = "[modes]" not in text[k]
    checks["A hosts the custom mode (MatchConfig modeId)"] = "DRAKAN_NET_HOST" in ta and (
        re.search(r"server: player 2 has 2 lives left", ta) is not None
    )
    checks["B's lives count down 2, 1 (server)"] = all(
        re.search(rf"server: player 2 has {n} li(ves|fe) left", ta) for n in (2, 1)
    )
    checks["B is eliminated (server)"] = "server: player 2 eliminated (out of lives)" in ta
    checks["B knows it is eliminated (Score lives 0)"] = (
        re.search(r"score: player 2 .* lives 0 \(eliminated\)", tb) is not None
    )
    elim = ta.find("server: player 2 eliminated")
    over = ta.find("server: phase -> results", elim) if elim >= 0 else -1
    respawn_after = re.search(r"respawn: player 2 at spawn", ta[elim:over]) if elim >= 0 and over > elim else None
    checks["B is not respawned after the elimination (until the results)"] = (
        elim >= 0 and over > elim and respawn_after is None
    )
    checks["the server ends it: last one standing, A wins"] = (
        "server: last one standing" in ta and "server: match over: player 1 wins" in ta
    )
    for k in "ab":
        checks[f"{k}: match over (last one standing): player 1 wins"] = (
            re.search(r"match over \(last one standing\): player 1 wins", text[k]) is not None
        )
    taken = re.search(r"server: pickup (\d+) \((.*?)\) taken by player 1, does not come back \(game mode\)", ta)
    checks["a taken pickup does not come back (pickup_respawn = never)"] = taken is not None and (
        re.search(rf"pickup {taken.group(1)} \(.*\) is back", ta + tb) is None if taken else False
    )
    for k in "ab":
        for line in text[k].splitlines():
            if any(s in line for s in ("lives", "eliminated", "last one standing", "match over", "does not come back")):
                print(f"{k.upper()} {line.strip()}")
    (out / "summary.json").write_text(json.dumps({"checks": checks}, indent=1), encoding="utf-8")
    for k, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + k)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
