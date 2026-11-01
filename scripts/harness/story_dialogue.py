"""Story dialogue proof: a real conversation in Surdana, stock and with an editor-built Story pack.

    python scripts/harness/story_dialogue.py --iso "<game>.iso"            # build pack, run both, assert
    python scripts/harness/story_dialogue.py --iso ... --skip-pack         # reuse build/story-dialogue/mods

How a conversation starts (decoded, see docs/reference/level-format.md "Starting a conversation"): Use (L1 / key E) must be a
quick tap (press and release inside 0.5 s of game time), Rynn must stand within the NPC's "Maximum conversation distance"
(1 lu = 2048 units) of the NPC, in front of it (its forward axis points at her) and facing it. The Surdana farmer is NPC #5102
at (613788, 152432, 433259) facing (0.342, 0, 0.940); the harness stands Rynn 600 units in front of it (DRAKAN_PLAYTEST_POS,
yaw 0.349 faces back at the farmer) and taps E.

Runs (play_run.py, one game at a time, private play folder, never dist/DrakanPC):
  stock  -> asserts the farmer's first line, Rynn's reply and that her stock sound 153 is requested;
  edited -> pack built by the editor's own code (scripts/harness/story_dialogue_pack.mts): a NEW dialogue line first,
            the farmer's second line with an EDITED subtitle, Rynn's reply with a SWAPPED sound (33 instead of 153).
Evidence: DRAKAN_TRACE_FN=4149a0 logs every SetSubtitle text ("[subtitle-set]"), the HLE log lists the sound requests of
LEVEL1ON.SDB (0304/<id>), screenshots of the subtitle bar are kept in the run folders.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "story-dialogue"
POS = "613993 152400 433823 0.349"  # 600 units in front of the Surdana farmer (NPC 5102), facing it

EDIT = "EDITED SUBTITLE: the editor changed what the farmer says."
NEW = "NEW LINE: the farmer says something nobody wrote."
FARMER1 = "Please, miss, can you help me?"
RYNN_STOCK = "Just calm down.  Where did you see her last?"
RYNN_SWAP = "What was that thing?"


def recipe() -> str:
    lines = ["0 sync play 60", "400 key down E", "402 key up E"]  # a tap shorter than the 0.5 s use window
    lines += [f"{f} shot s{f}" for f in range(560, 2560, 160)]
    lines.append("2600 quit")
    return "\n".join(lines) + "\n"


def prepare_play(play: Path, exe: Path) -> None:
    src = ROOT / "dist" / "DrakanPC"
    play.mkdir(parents=True, exist_ok=True)
    for f in src.iterdir():
        if f.is_file() and f.suffix.lower() in {".dll", ".ini"}:
            shutil.copy2(f, play / f.name)
    if (src / "ngx_data").is_dir() and not (play / "ngx_data").exists():
        shutil.copytree(src / "ngx_data", play / "ngx_data")
    shutil.copy2(exe, play / "drakan.exe")


def run_game(name: str, play: Path, mods: Path | None, timeout: int) -> Path:
    env = {
        "DRAKAN_PLAYTEST_POS": POS,
        "DRAKAN_TRACE_FN": "4149a0",
        "DRAKAN_TRACE_N": "400",
        "DRAKAN_HLE_LOG": "100000",
    }
    if mods:
        env["DRAKAN_MODS"] = str(mods)
    rec = OUT / f"{name}-recipe.txt"
    rec.write_text(recipe())
    cmd = [
        sys.executable,
        str(ROOT / "scripts/harness/play_run.py"),
        "--name",
        f"story-dialogue-{name}",
        "--exe",
        str(play / "drakan.exe"),
        "--recipe",
        str(rec),
        "--timeout",
        str(timeout),
        "--attempts",
        "2",
    ]
    for k, v in env.items():
        cmd += ["--env", f"{k}={v}"]
    subprocess.run(cmd, check=True)
    return ROOT / "build" / "harness" / f"story-dialogue-{name}"


def read_log(run: Path) -> str:
    return (
        (run / "stdout_1.log").read_text(encoding="latin-1")
        if (run / "stdout_1.log").exists()
        else (run / "stdout_2.log").read_text(encoding="latin-1")
    )


def subtitles(log: str) -> list[str]:
    return [m.group(1).rstrip() for m in re.finditer(r"\[subtitle-set\] speaker=\d+ text=(.*)", log)]


def sound_requests(log: str) -> set[int]:
    return {int(m.group(1)) for m in re.finditer(r"LEVEL1ON\.SDB 0304/(\d+)/", log)}


FAILS: list[str] = []


def check(ok: bool, what: str) -> None:
    print(("PASS  " if ok else "FAIL  ") + what)
    if not ok:
        FAILS.append(what)


def index(subs: list[str], prefix: str) -> int:
    for i, s in enumerate(subs):
        if s.startswith(prefix[:60]):
            return i
    return -1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--iso", type=Path, default=ROOT.parent / "Drakan - The Ancients' Gates.iso")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--skip-pack", action="store_true")
    ap.add_argument("--timeout", type=int, default=600)
    a = ap.parse_args()
    OUT.mkdir(parents=True, exist_ok=True)
    mods = OUT / "mods"
    if not a.skip_pack:
        if mods.exists():
            shutil.rmtree(mods)
        subprocess.run(
            ["npx", "tsx", "../scripts/harness/story_dialogue_pack.mts", str(a.iso), str(mods)],
            cwd=ROOT / "editor",
            check=True,
            shell=os.name == "nt",
        )
    play = OUT / "play"
    prepare_play(play, a.exe)

    stock = run_game("stock", play, None, a.timeout)
    slog = read_log(stock)
    ssubs = subtitles(slog)
    print("stock subtitles:", ssubs)
    check(index(ssubs, FARMER1) >= 0, "stock: conversation started, farmer line 1 shown")
    check(index(ssubs, RYNN_STOCK) > index(ssubs, FARMER1) >= 0, "stock: Rynn's reply shown after it")
    check(not any(s.startswith("EDITED") or s.startswith("NEW LINE") for s in ssubs), "stock: no edited text")
    check(153 in sound_requests(slog), "stock: sound 153 (Rynn's stock reply) requested")
    check(34 not in sound_requests(slog), "stock: sound 34 not requested")

    edited = run_game("edited", play, mods, a.timeout)
    elog = read_log(edited)
    esubs = subtitles(elog)
    print("edited subtitles:", esubs)
    n, e, r = index(esubs, NEW), index(esubs, EDIT), index(esubs, RYNN_SWAP)
    check("mods: loaded 'story.dialogue.proof'" in elog, "edited: pack loaded")
    check(n >= 0, "edited: NEW dialogue line spoken (its subtitle shown)")
    check(e > n >= 0, "edited: edited farmer subtitle shown after the new line")
    check(FARMER1 not in " ".join(esubs), "edited: stock farmer subtitle replaced")
    check(r > e >= 0, "edited: Rynn's reply plays the swapped sound (its subtitle is sound 33's)")
    check(RYNN_STOCK not in " ".join(esubs), "edited: Rynn's stock reply text absent")
    reqs = sound_requests(elog)
    check(34 in reqs, "edited: sound 34 (new line) requested")
    check(33 in reqs, "edited: sound 33 (swapped sound) requested")
    check(153 not in reqs, "edited: Rynn's stock sound 153 not requested")
    print(f"screenshots: {stock} {edited}")
    print("RESULT:", "FAILED " + str(len(FAILS)) if FAILS else "all checks passed")
    return 1 if FAILS else 0


if __name__ == "__main__":
    sys.exit(main())
