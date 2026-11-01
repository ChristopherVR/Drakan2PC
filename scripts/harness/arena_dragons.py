"""Two mounted players fight in an air arena (docs/reference/netcode.md "Air arenas: dragon breath PvP").

    python scripts/harness/arena_dragons.py --map arena-of-death-air --maps-file build/ootf-run/mp6/map.ini \
        --env DRAKAN_MODS=build/ootf-run/mp6/mods --prefix mp6 --name dragons1

Like arena_two_instances.py (same map / folder / env setup), but the players are on Arokh (the converted air arenas
start Rynn mounted): A holds the attack button (breath) while the test aid holds B's ghost in front of A, B only waits.
Checks: both mounted, A's breath projectile seen and claimed, the server accepted hits, B's dragon health fell, B died,
the kill was counted, the match ended with a winner and B respawned at a spawn point when the limit was not reached.
The game lock build/ootf-run/game.lock (mkdir) is held for the whole run, waiting up to 40 minutes for it.
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
LOCK = ROOT / "build" / "ootf-run" / "game.lock"
WINDOW = (1280, 720)


def log(msg: str) -> None:
    print(f"[dragon-harness] {msg}", flush=True)


def live_game() -> bool:
    out = subprocess.run(["tasklist", "/FI", "IMAGENAME eq drakan.exe"], capture_output=True, text=True).stdout
    return "drakan.exe" in out


def take_lock(wait_s: int = 2400) -> None:
    t0 = time.monotonic()
    while True:
        try:
            LOCK.mkdir()
            return
        except FileExistsError:
            age = time.time() - LOCK.stat().st_mtime
            if age > 1200 and not live_game():
                log("stale game lock (older than 20 min, no drakan.exe): taking it over")
                try:
                    LOCK.rmdir()
                except OSError:
                    pass
                continue
            if time.monotonic() - t0 > wait_s:
                raise SystemExit("game lock not available after 40 minutes") from None
            time.sleep(15)


def recipe(role: str, fire_s: int, delay: int, button: str, fly: bool = False) -> str:
    me = role
    lines = ["clock milliseconds", "0 sync play 60", f"2000 shot {me}_spawn", f"9000 shot {me}_arena"]
    t0 = 20000 + delay
    if role == "a":
        t = t0
        i = 0
        buttons = button.split(",")
        # hold the attack for 1.5 s, release for 0.5 s (a breath may need the button held); several buttons share the
        # window in turn
        while t < t0 + fire_s * 1000:
            b = buttons[min(len(buttons) - 1, (t - t0) * len(buttons) // (fire_s * 1000))]
            lines += [f"{t} button down {b}", f"{t + 1500} button up {b}"]
            if i % 4 == 1:
                lines.append(f"{t + 700} shot a_fire_{i:02d}")
            t += 2000
            i += 1
        lines += [f"{t + 4000} shot a_end", f"{t + 6000} quit"]
        if fly:
            lines += [f"{t0 - 1500} key down W", f"{t + 3000} key up W"]
    else:
        t = t0
        i = 0
        while t < t0 + fire_s * 1000 + 6000:
            lines.append(f"{t} shot b_s{i:02d}")
            t += 6000
            i += 1
        lines += [f"{t + 6000} quit"]
    return "\n".join(lines) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--map", required=True)
    ap.add_argument("--maps-file", type=Path, default=None)
    ap.add_argument("--name", default="dragons")
    ap.add_argument("--prefix", default="mp6")
    ap.add_argument("--port", type=int, default=47960)
    ap.add_argument("--frag-limit", type=int, default=2)
    ap.add_argument("--fire", type=int, default=40, help="seconds A holds the attack")
    ap.add_argument("--button", default="1", help="mouse button of the attack in the input script")
    ap.add_argument("--fly", action="store_true", help="A holds W (forward flight) while attacking")
    ap.add_argument("--timeout", type=int, default=200)
    ap.add_argument("--delay", type=int, default=0)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment for both")
    ap.add_argument("--env-a", action="append", default=[], metavar="K=V")
    a = ap.parse_args()

    m = find_map(a.map, a.maps_file)
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    for k in "ab":
        (out / k).mkdir(parents=True)
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.prefix}-{k}") for k in "ab"}
    for k in "ab":
        shutil.rmtree(exes[k].parent / "dev" / "world", ignore_errors=True)
        (out / k / "recipe.txt").write_text(recipe(k, a.fire, a.delay, a.button, a.fly), encoding="utf-8", newline="\n")
    common = {
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_MP_MAP": m["id"],
        "DRAKAN_ARENA_DUMP": "1",
        "DRAKAN_PVP_MAX_HIT_RANGE": "1000000",
        "DRAKAN_PVP_DRAGON_PROBE": "1",
        "DRAKAN_WINDOW_WIDTH": str(WINDOW[0]),
        "DRAKAN_WINDOW_HEIGHT": str(WINDOW[1]),
        "DRAKAN_WS_ASPECT": "16:9",
    }
    if a.maps_file:
        common["DRAKAN_MP_MAPS"] = str(a.maps_file.resolve())
    for kv in a.env:
        common[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    env_a = {
        **common,
        "DRAKAN_NET_HOST": str(a.port),
        "DRAKAN_NET_NAME": "Alpha",
        "DRAKAN_NET_FRAG_LIMIT": str(a.frag_limit),
        "DRAKAN_PVP_TEST_PLACE": "1500",
        "DRAKAN_PVP_TEST_FRONT": "1",
    }
    for kv in a.env_a:
        env_a[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    env_extra = {"a": env_a, "b": {**common, "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}", "DRAKAN_NET_NAME": "Bravo"}}
    procs: dict[str, subprocess.Popen] = {}
    logs = {k: out / k / "stdout_1.log" for k in "ab"}
    take_lock()
    log("game lock taken")
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
        try:
            LOCK.rmdir()
            log("game lock released")
        except OSError:
            pass
    text = {k: logs[k].read_bytes().decode("latin-1") for k in "ab"}
    checks: dict[str, bool] = {}
    for k in "ab":
        checks[f"{k}: arena world mode on"] = f"arena world mode on: {m['name']}" in text[k]
        checks[f"{k}: mounted (controlled actor is the dragon)"] = "dragon probe: flags90" in text[k]
    checks["A: breath seen and claimed"] = re.search(r"\[pvp\] breath hit player 2 .* -> claim", text["a"]) is not None
    checks["server accepted dragon hits"] = len(re.findall(r"claim: hit on player 2", text["a"])) >= 1
    checks["B hurt (dragon health)"] = re.search(r"\[pvp\] hit by player 1", text["b"]) is not None
    checks["B died"] = re.search(r"\[pvp\] killed by player 1", text["b"]) is not None
    checks["kill counted / match over"] = "match over" in text["a"]
    for k in "ab":
        for line in text[k].splitlines():
            if "[pvp]" in line or ("[arena]" in line and "spawn" in line):
                print(f"{k.upper()} {line.strip()}")
    for line in text["a"].splitlines():
        if "server:" in line or "match over" in line or "frag" in line:
            print(f"A {line.strip()}")
    (out / "summary.json").write_text(json.dumps({"checks": checks}, indent=1), encoding="utf-8")
    for k, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + k)
    print(out)
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
