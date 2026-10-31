"""Two game instances play a deathmatch in a multiplayer map (docs/reference/netcode.md "Multiplayer maps").

    python scripts/harness/arena_two_instances.py --map inquisitor-depths           # fight to the kill limit + a pickup
    python scripts/harness/arena_two_instances.py --map surdana-village --name arena_surdana --port 47950

A hosts through the DRAKAN_NET_HOST shortcut with the map (DRAKAN_MP_MAP), B joins (DRAKAN_NET_CONNECT). Both start a New
Game with the intro level's files replaced by the map's base level (DRAKAN_START_LEVEL_FROM = "Level 0 - Intro": straight
into the level, the same redirect the Multiplayer menu launch uses), so the run covers: the campaign removed (world
dumps `arena_before.json` / `arena_after.json` in each play folder's dev/world), spawn points chosen by the server, a
pickup taken by A (DRAKAN_ARENA_TEST_GOTO walks A onto it), B's copy removed, both copies back after DRAKAN_ARENA_PICKUP_SEC,
A fighting B's ghost to the kill limit (the test aids hold B's ghost in front of A and switch the server's reach check
off: the real players stand on spawn points far apart) and the match ending with results. Screenshots: the input
script's (a_*, b_*) and the module's own at every pickup event (arena_<n>_pickup<i>_<event>.png, DRAKAN_ARENA_SHOTS).
Private play folders build/<prefix>-a / -b, output build/harness/<name>/{a,b}/ and summary.json. No save card: New Game.
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
from arena_tour import find_map, summarize
from netplay_two_instances import game_env, prepare_folder

ROOT = Path(__file__).resolve().parents[2]
WINDOW = (1280, 720)


def log(msg: str) -> None:
    print(f"[arena-harness] {msg}", flush=True)


def recipe(role: str, fight_s: int) -> str:
    """Times are ms after each instance's `sync play`. Nobody opens the inventory: the arena gives and equips the
    starting loadout itself (drakan_arena armTick)."""
    a = role == "a"
    me = role
    lines = ["clock milliseconds", "0 sync play 60", f"2000 shot {me}_spawn", f"9000 shot {me}_arena"]
    if a:
        lines += [
            "12000 shot a_loadout",
            "22000 shot a_armed",
        ]  # the arena equips the loadout itself (no inventory taps)
        t = 24000
        timed = []
        for i in range(fight_s * 1000 // 900):
            t += 900
            timed += [(t, "button down 1"), (t + 60, "button up 1")]
            if i % 6 == 1:
                timed.append((t + 450, f"shot a_fight_{i:02d}"))
        lines += [f"{ts} {text}" for ts, text in timed]
        lines += [f"{t + 6000} shot a_end", f"{t + 8000} quit"]
    else:
        t = 4000
        i = 0
        lines += ["12000 shot b_loadout", "22000 shot b_armed"]
        t = 24000
        while t < fight_s * 1000 + 36000:
            lines.append(f"{t} shot b_s{i:02d}")
            t += 5000
            i += 1
        lines += [f"{t + 6000} quit"]
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", required=True)
    ap.add_argument("--maps-file", type=Path, default=None, help="an extra map file (DRAKAN_MP_MAPS), e.g. a test map")
    ap.add_argument("--name", default="")
    ap.add_argument("--prefix", default="arena-two")
    ap.add_argument("--port", type=int, default=47950)
    ap.add_argument("--frag-limit", type=int, default=3)
    ap.add_argument("--pickup", type=int, default=0, help="index of the pickup A takes (DRAKAN_ARENA_TEST_GOTO)")
    ap.add_argument("--pickup-sec", type=int, default=15, help="respawn time of the pickups (test aid)")
    ap.add_argument("--fight", type=int, default=70, help="seconds A swings")
    ap.add_argument(
        "--goto-ticks", type=int, default=5000, help="boundaries after the level pass when A walks onto the pickup"
    )
    ap.add_argument("--timeout", type=int, default=220)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment for both")
    a = ap.parse_args()

    m = find_map(a.map, a.maps_file)
    name = a.name or ("arena_pvp_" + m["id"])
    out = ROOT / "build" / "harness" / name
    if out.exists():
        shutil.rmtree(out)
    for k in "ab":
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-{k}") for k in "ab"}
    for k in "ab":  # the dumps of an earlier run must not pass for this one
        shutil.rmtree(exes[k].parent / "dev" / "world", ignore_errors=True)
        (out / k / "recipe.txt").write_text(recipe(k, a.fight), encoding="utf-8", newline="\n")

    common = {
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_MP_MAP": m["id"],
        "DRAKAN_ARENA_DUMP": "1",
        "DRAKAN_ARENA_SHOTS": "1",
        "DRAKAN_ARENA_PICKUP_SEC": str(a.pickup_sec),
        "DRAKAN_PVP_MAX_HIT_RANGE": "1000000",
        "DRAKAN_PVP_DAMAGE_SCALE": "8",
        "DRAKAN_WINDOW_WIDTH": str(WINDOW[0]),
        "DRAKAN_WINDOW_HEIGHT": str(WINDOW[1]),
        "DRAKAN_WS_ASPECT": "16:9",
    }
    if a.maps_file:
        common["DRAKAN_MP_MAPS"] = str(a.maps_file.resolve())
    for kv in a.env:
        common[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    env_extra = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_NET_FRAG_LIMIT": str(a.frag_limit),
            "DRAKAN_PVP_TEST_PLACE": "140",
            "DRAKAN_ARENA_TEST_GOTO": f"{a.pickup}@{a.goto_ticks}",
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
                log("timeout: killing the instances")
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") for k in "ab"}
    checks: dict[str, bool] = {}
    for k in "ab":
        t = text[k]
        checks[f"{k}: arena world mode on for '{m['name']}'"] = f"arena world mode on: {m['name']}" in t
        checks[f"{k}: campaign removed (creatures and NPCs, story objects)"] = (
            re.search(r"\[arena\] removed \d+ campaign actors \((\d+) creatures / NPCs, (\d+) story objects", t)
            is not None
        )
        checks[f"{k}: placed at a spawn point"] = re.search(r"\[arena\] player placed at spawn \d+", t) is not None
        checks[f"{k}: saving vetoed or never attempted"] = (
            "[lifecycle] save write vetoed" in t or "afterSaveWrite" not in t
        )
    spawn_a = re.findall(r"\[arena\] player placed at spawn (\d+): (\d+) \d+ (\d+)", text["a"])
    spawn_b = re.findall(r"\[arena\] player placed at spawn (\d+): (\d+) \d+ (\d+)", text["b"])
    checks["A and B were given different spawn points"] = bool(spawn_a and spawn_b) and spawn_a[0][0] != spawn_b[0][0]
    pick = a.pickup
    checks["A took the pickup (claim sent)"] = f"pickup {pick} (" in text["a"] and "taken here: claim" in text["a"]
    checks["the server accepted it"] = (
        re.search(rf"server: pickup {pick} \(.*\) taken by player 1", text["a"]) is not None
    )
    checks["B removed its copy"] = (
        re.search(rf"pickup {pick} \(.*\) taken by player 1: removed here", text["b"]) is not None
    )
    checks["the pickup is back on both"] = all(re.search(rf"pickup {pick} \(.*\) is back", text[k]) for k in "ab")
    checks["A claims hits and the match ends"] = (
        len(re.findall(r"claim: hit on player 2", text["a"])) >= 3 and "match over" in text["a"]
    )
    for k in "ab":
        d = {tag: exes[k].parent / "dev" / "world" / f"arena_{tag}.json" for tag in ("before", "after")}
        if all(p.is_file() for p in d.values()):
            before, after = summarize(d["before"]), summarize(d["after"])
            log(
                f"{k.upper()} world: {before['entities']} -> {after['entities']} entities, creatures with HP "
                f"{before['creatures']} -> {after['creatures']}, loose items {before['items']} -> {after['items']}"
            )
            checks[f"{k}: world dump shows no creatures after the filter"] = after["creatures"] == 0
            shutil.copy2(d["before"], out / k / "arena_before.json")
            shutil.copy2(d["after"], out / k / "arena_after.json")
        else:
            checks[f"{k}: world dumps written"] = False
    for k in "ab":
        for line in text[k].splitlines():
            if "[arena]" in line or ("pickup" in line and "[netplay" in line):
                print(f"{k.upper()} {line.strip()}")
    for line in text["a"].splitlines():
        if "server: pickup" in line or "match over" in line or "server: respawn" in line:
            print(f"A {line.strip()}")
    (out / "summary.json").write_text(json.dumps({"checks": checks}, indent=1), encoding="utf-8")
    for k, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + k)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
