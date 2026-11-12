"""Order of the Flame: Intro to the credits in ONE game process (tracking issue #5).

    python scripts/harness/ootf_full_run.py --name full1 --exe <own copy>/drakan.exe \\
        --mods build/ootf-pack-ps2tex --timeout 5400 [--env DRAKAN_PLAYTEST_GOD=1] [--shots] [--save-each]

Two segments, both in the same process, reported separately in result.json (`segments`):

1. Intro -> Volcanic: the DRAKAN_LEVELCHANGE probe takes the exits in turn (Intro, Ruined Village, Wartok Canyons, Grimstone
   Mines, Grotto, Islands, Alwarren, Rift Crystal, Islands Night, Succubus Lair, Volcanic), 1500 frames after each arrival.
   This is a load / transition test with FORCED exits, it does not earn the route (docs/reference/ootf-coop-harness.md).
2. Volcanic -> Rift World -> end sequence and credits: the earned route of ootf_volcanic_route.py (real inputs, each step
   checked in guest RAM). `--from` / `--to` pick steps of that route as there.

The pack folder needs ootf-levels.zip; mods.ini is written next to it. Exit status 0 when both segments passed (the last
route step passed and the guest did not crash). Requires the game lock like the other harnesses (mkdir
build/ootf-run/game.lock) when other agents share the machine.
"""

from __future__ import annotations

import re
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import live_drive as LD
import ootf_volcanic_route as VR

# Destination texts of the exits in order (the Intro's exit has no destination name: index 0). Ends at Volcanic.
CHAIN = [
    "0",
    "OotF 03 - Wartok Canyons",
    "OotF 04 - Grimstone Mines",
    "OotF 05 - Grotto",
    "OotF 06 - Islands",
    "OotF 07 - Alwarren",
    "OotF 08 - Rift Crystal",
    "OotF 09 - Islands Night",
    "OotF 10 - Succubus Lair",
    "OotF 11 - Volcanic",
]
START = "OotF 00 - Intro"
TARGET = "OotF 11 - Volcanic"
LOAD_RE = re.compile(r"afterLevelLoad level='(.*?)'")


def levels_loaded(text: str) -> list[str]:
    """The levels of the afterLevelLoad lines, in order."""
    return LOAD_RE.findall(text)


def chain_reached(levels: list[str], target: str = TARGET) -> bool:
    return bool(levels) and levels[-1] == target


def chain_env(steps: list[str] = CHAIN, after: int = 1500) -> str:
    return ",".join(steps) + f"@{after}"


class Full(VR.Route):  # pragma: no cover - needs a running game
    def chain_segment(self) -> dict:
        out = Path(self.args.out_root) / self.args.name
        log = out / "stdout.log"
        t0, seen = time.monotonic(), []
        while time.monotonic() - t0 < self.args.chain_timeout:
            if not self.link.alive():
                break
            text = log.read_bytes().decode("latin-1") if log.is_file() else ""
            seen = levels_loaded(text)
            if chain_reached(seen):
                break
            time.sleep(2.0)
        ok = chain_reached(seen)
        if ok:  # arrived: wait for control, then a few seconds for the level's opening
            self.link.wait(lambda s: bool(s.gameplay) and s.play_run >= 2, 120)
            self.link.wait_samples(300)
        seg = {"ok": ok, "seconds": round(time.monotonic() - t0), "levels": seen}
        self.note("chain", **seg)
        return seg

    def run(self) -> dict:
        seg1 = self.chain_segment()
        self.res["segments"] = {"chain": seg1}
        if not seg1["ok"]:
            self.res["failed_at"] = "chain"
            return self.res
        res = super().run()
        res["segments"] = {"chain": seg1, "route": {"ok": "failed_at" not in res}}
        return res


def mission_full(link, log, ctl, args) -> dict:  # pragma: no cover
    return Full(link, log, ctl, args).run()


LD.MISSIONS["full"] = mission_full


if __name__ == "__main__":  # pragma: no cover
    rest, opts = VR.split_args(sys.argv[1:])
    chain_timeout = 2400.0
    if "--chain-timeout" in rest:
        i = rest.index("--chain-timeout")
        chain_timeout = float(rest[i + 1])
        del rest[i : i + 2]
    mods = Path(rest[rest.index("--mods") + 1]) if "--mods" in rest else None
    if mods is not None:
        (mods / "mods.ini").write_text("[mods]\nootf-levels = on\n", encoding="utf-8")
    args = LD.parse_args(
        [
            *rest,
            "--level", START,
            "--env", "DRAKAN_GAME=d1",
            "--env", f"DRAKAN_LEVELCHANGE={chain_env()}",
            "idle",
        ]
    )  # fmt: skip
    args.mission = "full"
    args.chain_timeout = chain_timeout
    args.route_from, args.route_to = opts["from"], opts["to"]
    args.ending_seconds, args.save_each, args.shots = float(opts["ending-seconds"]), opts["save-each"], opts["shots"]
    sys.exit(LD.run(args))
