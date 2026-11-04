"""Co-op stand-in characters: two instances, each shown to the other as a non-Rynn character (and optionally as a
mounted dragon pair), docs/journal/2026-10-31/coop-characters.md.

    python scripts/harness/coop_characters.py --name chars1 --char-a ee7 --char-b 1798
    python scripts/harness/coop_characters.py --name dragon1 --dragon-test

A hosts (DRAKAN_NET_HOST), B joins; both run protocol v7 (PlayerState.character). `--char-a` / `--char-b` are the roster
class ids (hex) each instance announces (DRAKAN_COOP_CHARACTER), so A's screen shows B as `--char-b` and B's screen shows A
as `--char-a`. Both walk toward each other a little and take screenshots; the exit status is 0 when both exited cleanly
and each log shows the ghost spawned as the other's character. Private play folders build/<folder>-a / -b (copied exe),
cards copied from the dagger fixture with Rynn moved to two spots 300 units apart. Output: build/harness/<name>/{a,b}/.
"""

from __future__ import annotations

import argparse
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from coop_two_instances import DEFAULT_CARD, make_fixture
from netplay_two_instances import ROOT, game_env, prepare_folder

SPOT_A = "475700,160700,487600"  # in front of a town door, no enemies near
SPOT_B = "476800,160700,488500"
NL = "\n"


def recipe(shot: str, walk: str, ms: int = 700) -> str:
    out = "clock milliseconds\n0 sync play 60\n"
    out += f"1500 shot pre{NL}"
    t = 14000
    out += f"{t} key down {walk}{NL}{t + ms} key up {walk}{NL}"
    for i in range(14):
        out += f"{t + 1500 + i * 1500} shot {shot}{i:02d}{NL}"
    out += f"{t + 1500 + 14 * 1500 + 1000} quit{NL}"
    return out


def motion_recipe(shot: str) -> str:
    """Exercise real controller jumps, left/right evasion and attacks; capture multiple phases of each motion."""
    out = "clock milliseconds\n0 sync play 60\n"
    out += (
        "12000 key down TAB\n12120 key up TAB\n0 sync screen 21 20\n"
        "500 key tap ENTER\n1000 key tap LEFT\n1300 key tap LEFT\n1600 key tap LEFT\n1900 key tap DOWN\n"
        "2400 key down E\n2700 key up E\n"
        "4200 shot equipped\n5000 key down TAB\n5200 key up TAB\n0 sync play 60\n1500 shot ready\n"
    )
    for start, label, direction in [
        (3000, "standing", ""),
        (6000, "forward", "W"),
        (9000, "left", "A"),
        (12000, "right", "D"),
        (15000, "back", "S"),
    ]:
        if direction:
            out += f"{start} key down {direction}\n"
        out += f"{start + 120} key down SPACE\n{start + 220} key up SPACE\n"
        for frame, delay in enumerate((160, 280, 400, 560, 760, 1000)):
            out += f"{start + delay} shot {shot}_{label}_{frame}\n"
        if direction:
            out += f"{start + 1300} key up {direction}\n"
    out += "18000 key down A\n18100 button down 1\n18200 button up 1\n"
    for frame in range(8):
        out += f"{18250 + frame * 100} shot {shot}_strafe_attack_{frame}\n"
    out += "19500 key up A\n21000 key down F5\n21100 key up F5\n24000 shot after_save\n26000 quit\n"
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="chars1")
    ap.add_argument("--folder", default="coopchar")
    ap.add_argument("--port", type=int, default=47955)
    ap.add_argument("--char-a", default="ee7", help="roster class id A plays as (hex; 0 = Rynn)")
    ap.add_argument("--char-b", default="1798", help="roster class id B plays as (hex; 0 = Rynn)")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--card", type=Path, default=DEFAULT_CARD)
    ap.add_argument("--timeout", type=int, default=75)
    ap.add_argument(
        "--motion", action="store_true", help="exercise actual jumps, directional evasion and split attacks"
    )
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment, both instances")
    ap.add_argument("--env-a", action="append", default=[], metavar="K=V")
    ap.add_argument("--env-b", action="append", default=[], metavar="K=V")
    a = ap.parse_args()

    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    keys = "ab"
    for k in keys:
        (out / k).mkdir(parents=True)
    cards = {"a": make_fixture(a.card, "ca", SPOT_A), "b": make_fixture(a.card, "cb", SPOT_B)}
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.folder}-{k}") for k in keys}
    recipes = (
        {k: motion_recipe(k) for k in keys} if a.motion else {"a": recipe("a_s", "W", 3000), "b": recipe("b_s", "A")}
    )
    for k in keys:
        (out / k / "recipe.txt").write_text(recipes[k], encoding="utf-8", newline="\n")
    common = {"DRAKAN_PVP": "0", "DRAKAN_GHOST_LOG": "1", "DRAKAN_NET_TRACE": "1"}
    if a.motion:
        common["DRAKAN_INVENTORY_SCREEN"] = "native"
    env = {
        "a": {
            **common,
            "DRAKAN_NET_HOST": str(a.port),
            "DRAKAN_NET_NAME": "Alpha",
            "DRAKAN_NET_MODE": "coop",
            "DRAKAN_COOP_CHARACTER": a.char_a,
        },
        "b": {
            **common,
            "DRAKAN_NET_CONNECT": f"127.0.0.1:{a.port}",
            "DRAKAN_NET_NAME": "Bravo",
            "DRAKAN_COOP_CHARACTER": a.char_b,
        },
    }
    for kv in a.env + a.env_a:
        env["a"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    for kv in a.env + a.env_b:
        env["b"][kv.split("=", 1)[0]] = kv.split("=", 1)[1]
    logs = {k: out / k / "stdout_1.log" for k in keys}
    procs = {}
    t0 = time.monotonic()
    try:
        for k in keys:
            e = game_env(out / k, exes[k].parent, out / k / "recipe.txt", a.timeout, env[k], cards[k])
            procs[k] = subprocess.Popen(
                [str(exes[k])], cwd=exes[k].parent, env=e, stdout=logs[k].open("wb"), stderr=subprocess.STDOUT
            )
            print(f"started {k.upper()} (pid {procs[k].pid})", flush=True)
            time.sleep(4)
        while any(p.poll() is None for p in procs.values()):
            if time.monotonic() - t0 > a.timeout + 40:
                print("timeout: killing", flush=True)
                for p in procs.values():
                    p.kill()
                break
            time.sleep(1)
    finally:
        for p in procs.values():
            if p.poll() is None:
                p.kill()
    text = {k: logs[k].read_bytes().decode("latin-1") if logs[k].is_file() else "" for k in keys}
    for k in keys:
        for line in text[k].splitlines():
            if re.search(
                r"\[ghost\] (spawn|world insert|weapon|id \d+ channels)|ghost of player|is now character", line
            ):
                print(f"{k.upper()} {line.strip()}")
    checks = {
        "both exited cleanly": all(procs[k].returncode == 0 and "[in] script quit" in text[k] for k in keys),
        "a: ghost of b spawned": "ghost of player" in text["a"] and "spawned" in text["a"],
        "b: ghost of a spawned": "ghost of player" in text["b"] and "spawned" in text["b"],
    }
    if a.motion:
        observations = []
        for key in keys:
            matches = re.findall(
                r"\[character-motion\] local=(\d) character=([0-9a-f]+) source=([0-9a-f]+)/([0-9a-f]+) "
                r"mapped=([0-9a-f]+)/([0-9a-f]+)",
                text[key],
            )
            observations += [(key, *(int(v, 16) for v in match)) for match in matches]
        for key in keys:
            for local in (0, 1):
                sources = {row[3] & 0xFFFF for row in observations if row[0] == key and row[1] == local}
                checks[f"{key}: {'local' if local else 'remote'} real left/right evasion"] = bool(
                    sources & {0x569, 0x5B5}
                ) and bool(sources & {0x56A, 0x5B7})
                checks[f"{key}: {'local' if local else 'remote'} standing jump / airborne / landing"] = (
                    bool(sources & {0x8A1, 0x8C8, 0x8CA, 0x8DF})
                    and bool(sources & {0x8A0, 0x680, 0x8E2, 0x8E5, 0x62D})
                    and bool(sources & {0x8A2, 0x8C9, 0x8CB, 0x8E4})
                )
        mapping = {}
        consistent = True
        for _key, _local, character, lower, upper, mapped_lower, mapped_upper in observations:
            for source, mapped in [(lower, mapped_lower), (upper, mapped_upper)]:
                identity = (character, source)
                consistent &= mapping.setdefault(identity, mapped) == mapped and 0x58000 <= mapped < 0x58E00
        checks["all local/remote source clips use identical generated mappings"] = bool(mapping) and consistent
        checks["both loaded generated motion pack"] = all("loaded 'drakan-character-motion'" in text[k] for k in keys)
        checks["both allocated independent native torso channels"] = all(
            re.search(r"split torso channel: actor .* channels 2", text[k]) is not None for k in keys
        )
    for name, ok in checks.items():
        print(("PASS " if ok else "FAIL ") + name)
    print(f"screenshots: {out}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    raise SystemExit(main())
