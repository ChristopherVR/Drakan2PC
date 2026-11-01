"""Map vote on the results screen in game (docs/reference/netcode.md "Map vote"): two real instances.

    python scripts/harness/mp_vote_check.py --port 28700 --previews build/harness/prevcache

A hosts a deathmatch on Surdana Meadow (22 s time limit, results stay 120 s), B joins. When the match ends by time both
players see three picture cards (the cached map pictures of --previews, DRAKAN_PREVIEW_DIR; maps without a picture show the
drawn sketch). A presses E twice (Q / E step the vote), B presses E twice: both vote for the second candidate. A presses
Back to lobby; the server applies the winner, so the lobby (and the next match) are on that map.

Private play folders build/mp-vote2-a / -b; output build/harness/<name>/{a,b}/ (logs, recipes, screenshots), summary.json.
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
import mp_results_check as res
import netplay_menu_two_instances as base
from netplay_menu_two_instances import ROOT, game_env, log, prepare_folder, start, text_of


def recipes() -> tuple[str, str]:
    a = res.host_lines(0)
    b = res.join_lines()
    a += ["0 sync play 60", "12000 shot a_playing", "30000 shot a_results_novote"]
    b += ["0 sync play 60", "12000 shot b_playing", "30000 shot b_results_novote"]
    # both: E, E (the second candidate); B first, then A
    b += ["32000 key tap E", "32500 key tap E", "34000 shot b_results_voted"]
    a += ["33000 key tap E", "33500 key tap E", "36000 shot a_results_voted", "38000 key tap ENTER"]  # Back to lobby
    a += [
        "0 sync screen 8 3",
        "2500 shot a_lobby_after_vote",
        "6000 key tap DOWN",
        "6600 key tap ENTER",
        "0 sync play 60",
    ]
    a += ["6000 shot a_next_match", "9000 quit"]
    b += ["0 sync screen 8 3", "2500 shot b_lobby_after_vote", "0 sync play 60", "6000 shot b_next_match", "9000 quit"]
    return "\n".join(a) + "\n", "\n".join(b) + "\n"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", type=int, default=28700)
    ap.add_argument("--name", default="mp_vote")
    ap.add_argument("--previews", type=Path, default=ROOT / "build" / "harness" / "prevcache")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--timeout", type=int, default=420)
    ap.add_argument("--size", default="1280x720")
    a = ap.parse_args()
    base.WINDOW[:] = [int(v) for v in a.size.lower().split("x")]
    base.PORT[:] = [a.port]
    run = ROOT / "build" / "harness" / a.name
    folders = {"a": ROOT / "build" / "mp-vote2-a", "b": ROOT / "build" / "mp-vote2-b"}
    exes = {k: prepare_folder(base.SRC, a.exe, v) for k, v in folders.items()}
    for k in ("a", "b"):
        (run / k).mkdir(parents=True, exist_ok=True)
        for stale in [*(run / k).glob("*.png"), *(run / k).glob("*.bmp")]:
            stale.unlink()
    ra, rb = recipes()
    (run / "a" / "recipe.txt").write_text(ra, encoding="utf-8", newline="\n")
    (run / "b" / "recipe.txt").write_text(rb, encoding="utf-8", newline="\n")
    env_a = game_env(run / "a", folders["a"], run / "a" / "recipe.txt", a.timeout, "HostRynn")
    env_b = game_env(run / "b", folders["b"], run / "b" / "recipe.txt", a.timeout, "JoinRynn")
    for e in (env_a, env_b):
        e["DRAKAN_NO_ACTIVATE"] = "0"
        e["DRAKAN_PREVIEW_DIR"] = str(a.previews.resolve()) + "/"
        e["DRAKAN_MP_CAPTURE"] = "0"  # the pictures come from --previews
    env_a["DRAKAN_MP_RESULTS_SEC"] = "120"
    env_a["DRAKAN_MP_TIME_LIMIT_SEC"] = "22"
    pa = start(exes["a"], env_a, run / "a")
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
    checks = {
        "the match ended by time on both": all(re.search(r"match over \(time limit\)", t) for t in (ta, tb)),
        "the server opened the vote with candidates": "match over" in ta,
        "A voted (E, E)": len(re.findall(r"\[results\] vote for map", ta)) >= 1,
        "B voted (E, E)": len(re.findall(r"\[results\] vote for map", tb)) >= 1,
        "the server counted votes from both": len(re.findall(r"vote: player \d+ voted", ta)) >= 2,
        "A pressed Back to lobby": "results: back to the lobby (host)" in ta,
        "the vote chose a map": bool(re.search(r"vote result: ", ta)),
        "the next match loaded the voted map": bool(re.search(r"server: next map: ", ta)),
    }
    for k, v in checks.items():
        log(f"{'ok  ' if v else 'FAIL'} {k}")
    for line in ta.splitlines():
        if re.search(r"vote: player|vote result|next map:|listening on UDP", line):
            log("A: " + line.strip()[:160])
    (run / "summary.json").write_text(json.dumps({"checks": checks}, indent=1), encoding="utf-8", newline="\n")
    log(f"output: {run}")
    return 0 if all(checks.values()) else 1


if __name__ == "__main__":
    sys.exit(main())
