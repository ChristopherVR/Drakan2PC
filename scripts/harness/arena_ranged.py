"""Ranged PvP in an arena, two instances (docs/reference/netcode.md "Ranged PvP in the arena").

    python scripts/harness/arena_ranged.py --port 27200

Alpha (host) gets a bow and arrows (DRAKAN_ARENA_LOADOUT, DRAKAN_ARENA_EQUIP=bow: the arena raises the bow skill the
bow asks for and equips it), Bravo gets the spells (DRAKAN_ARENA_SPELL=0: the debug console's `shaman` unlocks them,
the game's selector arms spell 0). Each fires at the other (DRAKAN_PVP_TEST_PLACE / _FRONT hold the other's ghost in front
of the shooter, the server's reach check is off): the projectile scan sees the arrow / spell cross the ghost, the claim
is accepted by the server (weapon 2), the victim's instance takes the hit and the match ends with the kills on the
scoreboard. Private play folders build/arena-ranged-{a,b}, output build/harness/<name>/{a,b}/ + summary.json.
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
MAPS = ROOT / "scripts" / "harness" / "maps" / "arena_ranged_test.ini"


def log(msg: str) -> None:
    print(f"[arena-ranged] {msg}", flush=True)


def recipe(role: str, fire_s: int, start_ms: int, adopt: bool = False, a_delay_ms: int = 0) -> str:
    """Times are ms after `sync play`. A draws and releases the bow (attack held 1.3 s) every 3 s, B taps attack (a
    cast) every 3 s; both hold F4 (scoreboard) near the end."""
    a = role == "a"
    lines = ["clock milliseconds", "0 sync play 60", f"2000 shot {role}_spawn", f"9000 shot {role}_arena"]
    if (
        a and not adopt
    ):  # the bow lies at A's feet: walk into it (the engine picks it up by touch), equipping follows by itself
        for t0, hold in ((12000, 500), (15000, 400)):  # short: past the bow is a lake
            lines += [f"{t0} key down W", f"{t0 + hold} key up W"]
        lines += ["22000 shot a_bow_picked"]
    lines += [f"{start_ms - 4000} shot {role}_armed"]
    t = start_ms + (a_delay_ms if a else 0)
    n = 0
    while t < start_ms + a_delay_ms + fire_s * 1000:
        if a:
            lines += [f"{t} button down 1", f"{t + 1300} button up 1"]
        else:
            lines += [f"{t} button down 1", f"{t + 150} button up 1"]
        if n % 2 == 1:
            lines.append(f"{t + (1700 if a else 900)} shot {role}_fire_{n:02d}")
        t += 3000
        n += 1
    lines += [f"{t + 500} key down F4", f"{t + 2500} shot {role}_scoreboard", f"{t + 3000} key up F4"]
    lines += [f"{t + 6000} shot {role}_end", f"{t + 8000} quit"]
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", default="", help="default: ranged-test, or ranged-adopt with --adopt")
    ap.add_argument(
        "--adopt",
        action="store_true",
        help="Alpha's bow is the level's own bow filed into the inventory at the start (DRAKAN_ARENA_LOADOUT_ADOPT), "
        "no walking onto it",
    )
    ap.add_argument("--maps-file", type=Path, default=MAPS)
    ap.add_argument("--name", default="arena_ranged")
    ap.add_argument("--prefix", default="arena-ranged")
    ap.add_argument("--port", type=int, default=27200)
    ap.add_argument("--frag-limit", type=int, default=8)
    ap.add_argument("--scale", type=int, default=16, help="DRAKAN_PVP_DAMAGE_SCALE: 16 = a spell (10) kills at once")
    ap.add_argument("--fire", type=int, default=90, help="seconds of shooting")
    ap.add_argument(
        "--a-delay",
        type=int,
        default=10,
        help="seconds Alpha waits after Bravo starts casting (spells land rarely; the arrows would end the match first)",
    )
    ap.add_argument(
        "--b-place", type=int, default=350, help="distance of Alpha's ghost in front of Bravo (DRAKAN_PVP_TEST_PLACE)"
    )
    ap.add_argument("--start", type=int, default=36000, help="ms after play when shooting starts")
    ap.add_argument("--timeout", type=int, default=200)
    ap.add_argument("--spell", type=int, default=0, help="spell index B selects")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment for both")
    a = ap.parse_args()

    m = find_map(a.map or ("ranged-adopt" if a.adopt else "ranged-test"), a.maps_file)
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    for k in "ab":
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-{k}") for k in "ab"}
    for k in "ab":
        shutil.rmtree(exes[k].parent / "dev" / "world", ignore_errors=True)
        (out / k / "recipe.txt").write_text(
            recipe(k, a.fire, a.start, a.adopt, a.a_delay * 1000), encoding="utf-8", newline="\n"
        )
    common = {
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_MP_MAP": m["id"],
        "DRAKAN_MP_MAPS": str(a.maps_file.resolve()),
        "DRAKAN_ARENA_SHOTS": "1",
        "DRAKAN_PVP_MAX_HIT_RANGE": "1000000",
        "DRAKAN_PVP_DAMAGE_SCALE": str(a.scale),
        "DRAKAN_PVP_TEST_PLACE": "700",
        "DRAKAN_PVP_TEST_FRONT": "1",
        "DRAKAN_WINDOW_WIDTH": "1280",
        "DRAKAN_WINDOW_HEIGHT": "720",
        "DRAKAN_WS_ASPECT": "16:9",
    }
    for kv in a.env:
        common[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    env_extra = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_NET_FRAG_LIMIT": str(a.frag_limit),
            "DRAKAN_ARENA_EQUIP": "bow",
            **({"DRAKAN_ARENA_LOADOUT_ADOPT": "425126,160500,441806"} if a.adopt else {}),
        },
        "b": {
            **common,
            "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}",
            "DRAKAN_NET_NAME": "Bravo",
            "DRAKAN_PVP_TEST_PLACE": str(a.b_place),
            "DRAKAN_ARENA_SPELL": str(a.spell),
        },
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
                log("timeout: stopping the instances (by pid)")
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
    if a.adopt:
        checks["A: the level's bow was filed in as the loadout"] = (
            "[arena] loadout adopt:" in ta and "[arena] loadout: 1 items given" in ta
        )
    else:
        checks["A: picked the bow up from the ground (claim sent)"] = "taken here: claim" in ta
    checks["A: bow equipped"] = (
        "[arena] armed: the bow" in ta or re.search(r"local weapons: right \w+ left 31bc9", ta) is not None
    )
    checks["B: spells unlocked and spell selected"] = re.search(r"\[arena\] spell: selected 0", tb) is not None
    # A's arrows -> B
    a_proj = re.findall(r"\[pvp\] projectile \w+ class 1005f damage property (\d+) -> claim damage (\d+)", ta)
    a_cross = re.findall(r"\[pvp\] arrow \w+ hit ghost \d+ \(player 2\) damage (\d+) -> claim", ta)
    a_claims = re.findall(r"claim: hit on player 2, weapon 2, damage (\d+)", ta)
    a_hits = re.findall(r"hit: player 1 hit player 2 for (\d+) \(weapon 2\), health now (-?\d+)", ta)
    b_hurt = re.findall(r"\[pvp\] (?:hit|killed) by player 1\b", tb)
    checks["A: arrows left the bow (projectile scan)"] = bool(a_proj)
    checks["A: an arrow crossed B's ghost and became a claim"] = bool(a_cross) and bool(a_claims)
    checks["server: accepted the arrow hits (weapon 2)"] = bool(a_hits)
    checks["B: HP drop replicated to the victim instance (hit by player 1)"] = bool(b_hurt)
    # B's spells -> A
    b_proj = re.findall(r"\[pvp\] projectile \w+ class 10008 damage property (\d+) -> claim damage (\d+)", tb)
    b_cross = re.findall(r"\[pvp\] spell \w+ hit ghost \d+ \(player 1\) damage (\d+) -> claim", tb)
    b_hits = re.findall(r"hit: player 2 hit player 1 for (\d+) \(weapon 2\), health now (-?\d+)", ta)
    a_hurt = re.findall(r"\[pvp\] (?:hit|killed) by player 2\b", ta)
    checks["B: a spell projectile appeared (projectile scan)"] = bool(b_proj)
    checks["B: a spell crossed A's ghost and became a claim"] = bool(b_cross)
    checks["server: accepted the spell hits (weapon 2)"] = bool(b_hits)
    checks["A: HP drop replicated to the victim instance (hit by player 2)"] = bool(a_hurt)
    kills = re.findall(r"death: player (\d) killed by player (\d)", ta)
    checks["scoreboard: a kill by A's arrows is credited"] = ("2", "1") in kills
    checks["scoreboard: a kill by B's spells is credited"] = ("1", "2") in kills
    checks["the match ends"] = "match over" in ta
    print(
        f"A projectiles {a_proj[:3]} crossings {a_cross[:3]} claims {a_claims[:3]} accepted {a_hits[:4]} B hurt {b_hurt[:4]}"
    )
    print(f"B projectiles {b_proj[:3]} crossings {b_cross[:3]} accepted {b_hits[:4]} A hurt {a_hurt[:4]}")
    print(f"kills {kills}")
    for k in "ab":
        for line in text[k].splitlines():
            if "[arena] loadout" in line or "[arena] armed" in line or "[arena] spell" in line or "skill" in line:
                print(f"{k.upper()} {line.strip()}")
    (out / "summary.json").write_text(json.dumps({"checks": checks}, indent=1), encoding="utf-8")
    for k, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + k)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
