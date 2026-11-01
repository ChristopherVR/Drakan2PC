"""Make the co-op character turntables (docs/journal/2026-11-01/character-previews.md): one game instance loads Surdana with
the player standing still, DRAKAN_CHAR_PREVIEWS spawns each roster character beside her as a ghost stand-in, turns it in 12
steps and cuts it out of the window shots; the strips land in <previews>/chars/<class id hex4>.png.

    python scripts/harness/char_previews.py --dir build/harness/charcache --ids ee7,1798   # a few characters
    python scripts/harness/char_previews.py --dir build/harness/charcache                  # the whole roster (minutes)

The player's own run does the same when DRAKAN_CHAR_PREVIEWS=all is set in a level. Exit status 0 when every asked-for
strip exists. Output: build/harness/<name>/ (log, window shots).
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

SPOT = "475700,160700,487600"  # in front of a town door, no enemies near


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="char_previews")
    ap.add_argument("--dir", type=Path, default=ROOT / "build" / "harness" / "charcache", help="DRAKAN_PREVIEW_DIR")
    ap.add_argument("--ids", default="", help="hex class ids, comma separated (default: the whole roster)")
    ap.add_argument("--max", type=int, default=0, help="the first N roster characters only")
    ap.add_argument("--left", action="store_true", help="stand the character on the other side of the player")
    ap.add_argument("--timeout", type=int, default=0)
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--card", type=Path, default=DEFAULT_CARD)
    a = ap.parse_args()
    ids = [i for i in a.ids.split(",") if i]
    timeout = a.timeout or (120 + 60 * (len(ids) or a.max or 40))
    out = ROOT / "build" / "harness" / a.name
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    exe = prepare_folder(a.src, a.exe, ROOT / "build" / "charstudio")
    recipe = out / "recipe.txt"
    recipe.write_text(
        "clock milliseconds\n0 sync play 60\n" + f"{timeout * 1000 - 5000} quit\n", encoding="utf-8", newline="\n"
    )
    card = make_fixture(a.card, "chs", SPOT)
    env = {
        "DRAKAN_PREVIEW_DIR": str(a.dir.resolve()),
        "DRAKAN_CHAR_PREVIEWS": ",".join(f"{int(i, 16):04x}" for i in ids)
        if ids
        else "all",  # "0" alone would mean off
        "DRAKAN_ARENA_LOG": "1",
        "DRAKAN_GHOST_LOG": "1",
    }
    if a.max:
        env["DRAKAN_CHAR_PREVIEW_MAX"] = str(a.max)
    if a.left:
        env["DRAKAN_CHAR_PREVIEW_LEFT"] = "1"
    e = game_env(out, exe.parent, recipe, timeout, env, card)
    log = out / "stdout_1.log"
    p = subprocess.Popen([str(exe)], cwd=exe.parent, env=e, stdout=log.open("wb"), stderr=subprocess.STDOUT)
    print(f"started (pid {p.pid}), timeout {timeout}s", flush=True)
    t0 = time.monotonic()
    while p.poll() is None:
        if time.monotonic() - t0 > timeout + 60:
            p.kill()
            break
        time.sleep(2)
        if "character previews: finished" in log.read_bytes().decode("latin-1"):
            time.sleep(3)
            p.terminate()
            break
    text = log.read_bytes().decode("latin-1")
    for line in text.splitlines():
        if re.search(r"character previews|\[ghost\] spawn refused", line):
            print(line.strip())
    want = ids or []
    ok = all((a.dir.resolve() / "chars" / f"{int(i, 16):04x}.png").is_file() for i in want) and "finished" in text
    print("PASS" if ok else "FAIL", "strips in", a.dir.resolve() / "chars")
    return 0 if ok else 1


if __name__ == "__main__":
    raise SystemExit(main())
