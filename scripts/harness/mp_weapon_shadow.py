"""Multiplayer weapon shadow and hand pose: two instances, each playing a non-Rynn character with an equipped weapon,
look down at the ground so their own shadow (and the partner's) is in view, then zoom in on the weapon hand.

    python scripts/harness/mp_weapon_shadow.py --name wshadow1
    python scripts/harness/mp_weapon_shadow.py --name wshadow-rynn --char-a 0      (Rynn baseline for comparison)

A hosts co-op (DRAKAN_NET_HOST), B joins. Both open the native inventory and equip the first weapon (the recipe of
coop_characters.py --motion), close it, then:

* `ground_<pitch>_<n>`: the camera pitched down in steps (mouse dy), idle stance and mid-swing, shadow on the ground;
* `hand_<n>`: zoomed in (wheel) behind the weapon hand, for the grip / finger pose;
* `partner_<n>`: turned toward the partner, so the remote ghost's shadow is in view as well.

`DRAKAN_SHADOW_TRACE` logs every native character-projector call of the first frames after the weapon is equipped
(`[shadow-trace] ... role ... ghost ...`). The summary counts, per frame, how many projections each avatar part got:
a held item projected on its own next to the body that already carries it is a duplicate weapon shadow.
Output: build/harness/<name>/{a,b}/ (screenshots, drakan.log, stdout_1.log, shadow_summary.json).
"""

from __future__ import annotations

import argparse
import collections
import json
import re
import shutil
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from coop_characters import SPOT_A, SPOT_B
from coop_two_instances import DEFAULT_CARD, make_fixture
from netplay_two_instances import ROOT, game_env, prepare_folder

NL = "\n"
# The shared dagger fixture card (coop_two_instances.DEFAULT_CARD) is a harness output that may be absent: fall back to
# another saved Surdana dagger card. make_fixture reads <card>.parent/mc0.
FALLBACK_CARD = ROOT / "build" / "harness" / "rt13-dir-always" / "card" / "mc0"


def default_card() -> Path:
    surdana = "mc0/BESCES-50006Slot1/Level 1A - Surdana.rsg"
    return DEFAULT_CARD if (DEFAULT_CARD.parent / surdana).is_file() else FALLBACK_CARD


EQUIP = (
    "12000 key down TAB\n12120 key up TAB\n0 sync screen 21 20\n"
    "500 key tap ENTER\n1000 key tap LEFT\n1300 key tap LEFT\n1600 key tap LEFT\n1900 key tap DOWN\n"
    "2400 key down E\n2700 key up E\n"
    "4200 shot equipped\n5000 key down TAB\n5200 key up TAB\n0 sync play 60\n"
)


def recipe(k: str, partner_turn: int) -> str:
    out = "clock milliseconds\n0 sync play 60\n" + EQUIP
    t = 1500
    out += f"{t} shot {k}_ready\n"
    # Pitch down in steps; each step is a shot at rest and one mid-swing.
    for step in range(4):
        t += 800
        out += f"{t} mouse 0 18 10\n"
        t += 700
        out += f"{t} shot {k}_ground_{step}_idle\n"
        out += f"{t + 100} button down 1\n{t + 200} button up 1\n"
        for frame, delay in enumerate((350, 550, 800)):
            out += f"{t + delay} shot {k}_ground_{step}_swing{frame}\n"
        t += 1600
    # Hands: a lower view from behind (the summary crops the avatar); idle frames 300 ms apart show finger motion.
    # (The mouse wheel is next / previous weapon, not camera zoom.)
    t += 500
    out += f"{t} mouse 0 -40 10\n"
    t += 1200
    for i in range(8):
        out += f"{t + i * 300} shot {k}_hand_{i}\n"
    t += 2600
    out += f"{t} button down 1\n{t + 100} button up 1\n"
    for i, delay in enumerate((250, 450, 700)):
        out += f"{t + delay} shot {k}_hand_swing{i}\n"
    t += 1500
    # Putting the weapon away and switching weapons, looking down at the shadows: stand idle (the game may sheathe on
    # its own), walk, then next weapon / previous weapon (mouse wheel) and quick slot 1 pressed on the drawn weapon.
    out += f"{t} mouse 0 30 10\n"
    t += 800
    for i in range(5):
        out += f"{t + i * 2000} shot {k}_idle_{i}\n"
    t += 10000
    out += f"{t} key down W\n"
    for i in range(4):
        out += f"{t + 300 + i * 600} shot {k}_walk_{i}\n"
    out += f"{t + 2800} key up W\n"
    t += 3500
    for label, action in (("next", "wheel -1"), ("prev", "wheel 1"), ("quick1", "key tap 1"), ("quick1b", "key tap 1")):
        out += f"{t} {action}\n"
        for i, delay in enumerate((200, 500, 900, 1400, 2200)):
            out += f"{t + delay} shot {k}_{label}_{i}\n"
        t += 3000
    # Turn toward the partner (each stands ~1400 units from the other) and look at both shadows.
    t += 1500
    out += f"{t} mouse {partner_turn} 20 12\n"
    for i, delay in enumerate((800, 1600)):
        out += f"{t + delay} shot {k}_partner_{i}\n"
    out += f"{t + 3000} quit\n"
    return out


def summarize(text: str) -> dict:
    """Per tick, projections by role; a frame where a held item is projected separately is a duplicate candidate."""
    per_tick: dict[int, collections.Counter] = collections.defaultdict(collections.Counter)
    rx = re.compile(
        r"\[shadow-trace\] tick (\d+) source ([0-9a-f]+) res ([0-9a-f]+) role (\S+) ghost (\d+) ?(\S*) caster ([0-9a-f]+)"
    )
    for m in rx.finditer(text):
        tick, _src, _res, role, ghost, part, caster = m.groups()
        label = part or role
        per_tick[int(tick)][label + ("" if caster != "0" else "(suppressed)")] += 1
    totals = collections.Counter()
    for c in per_tick.values():
        totals.update(c)
    item_frames = sum(1 for c in per_tick.values() if any("item" in k and "suppressed" not in k for k in c))
    return {
        "ticks": len(per_tick),
        "projections_by_part": dict(totals),
        "ticks_with_separately_projected_item": item_frames,
        "sample": {str(t): dict(c) for t, c in list(per_tick.items())[:6]},
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="wshadow1")
    ap.add_argument("--folder", default="wshadow")
    ap.add_argument("--port", type=int, default=47965)
    ap.add_argument("--char-a", default="16bf", help="roster class id A plays as (hex; 0 = Rynn; default Monk)")
    ap.add_argument("--char-b", default="166a", help="roster class id B plays as (hex; default Ranwulf)")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--card", type=Path, default=default_card())
    ap.add_argument("--timeout", type=int, default=170)
    ap.add_argument("--trace", type=int, default=4000, help="DRAKAN_SHADOW_TRACE: projector calls to log")
    ap.add_argument("--env", action="append", default=[], metavar="K=V", help="extra environment, both instances")
    a = ap.parse_args()

    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    keys = "ab"
    for k in keys:
        (out / k).mkdir(parents=True)
    cards = {"a": make_fixture(a.card, "wa", SPOT_A), "b": make_fixture(a.card, "wb", SPOT_B)}
    exes = {k: prepare_folder(a.src, a.exe, ROOT / "build" / f"{a.folder}-{k}") for k in keys}
    for k, turn in (("a", 40), ("b", -40)):
        (out / k / "recipe.txt").write_text(recipe(k, turn), encoding="utf-8", newline="\n")
    common = {
        "DRAKAN_PVP": "0",
        "DRAKAN_INVENTORY_SCREEN": "native",
        "DRAKAN_SHADOW_TRACE": str(a.trace),
        "DRAKAN_PAUSE_UNFOCUSED": "0",
        "DRAKAN_GHOST_LOG": "1",
        "DRAKAN_NET_TRACE": "1",
    }
    for kv in a.env:
        common[kv.split("=", 1)[0]] = kv.split("=", 1)[1]
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
    ok = True
    for k in keys:
        summary = summarize(text[k])
        (out / k / "shadow_summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
        weapons = re.findall(r"\[ghost\] weapon record \w+ -> item \w+ \(ghost \d+ hand \d+ bone \d+\)", text[k])
        print(f"{k.upper()}: {len(weapons)} weapon attachments; shadow projections {summary['projections_by_part']}")
        print(
            f"{k.upper()}: frames with a separately projected held item: {summary['ticks_with_separately_projected_item']}"
            f" of {summary['ticks']}"
        )
        clean = procs[k].returncode == 0 and "[in] script quit" in text[k]
        shots = sorted(p.name for p in (out / k).glob("*.png"))
        print(f"{k.upper()}: exited cleanly={clean}, {len(shots)} screenshots")
        ok &= clean and bool(weapons) and bool(shots)
    print(f"output: {out}")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
