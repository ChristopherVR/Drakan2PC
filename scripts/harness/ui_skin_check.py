"""UI skin proof (docs/reference/modding.md "Menus and HUD"): runs the game twice per screen, once as shipped and once
with a mod pack (built here under build/harness/ui_skin/mods/ui.demo) that renames a main-menu entry, reorders the
pause menu and changes the HUD / theme accent, and takes a screenshot of the title menu and the pause menu each time.

    python scripts/harness/ui_skin_check.py [--exe build/final-game/bin/drakan.exe] [--timeout 90]

Output: build/harness/ui_skin/<base|mod>_<title|pause>/ (stdout_1.log, *.png). Exit status 0 when the mod runs log the
skin being loaded and the screenshots exist.
"""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import game_env, prepare_folder

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "build" / "harness" / "ui_skin"

SKIN = """[theme]
accent = #20C0A0

[hud]
accent = #FF3366

[rows]
title.main = new_game, continue, load_game, options, credits, quit
pause.root = resume, options, save, load, quit

[strings]
title.row.new_game = Begin Anew
title.desc.new_game = A fresh start.
pause.row.resume = Return to Game
"""
MANIFEST = 'id = "ui.demo"\nname = "UI skin demo"\nversion = "1.0.0"\n\n[target]\nprofile = "*"\n'

RECIPES = {
    "title": "clock milliseconds\n0 sync screen 8 3\n2500 shot title_main\n3500 key tap DOWN\n4500 shot title_second\n5500 quit\n",
    "pause": "clock milliseconds\n0 sync play 60\n3000 key tap ESC\n5000 shot pause\n5500 key tap DOWN\n6500 shot pause_second\n7000 quit\n",
}


def build_mod() -> Path:
    mods = OUT / "mods"
    pack = mods / "ui.demo"
    (pack / "ui").mkdir(parents=True, exist_ok=True)
    (pack / "mod.toml").write_text(MANIFEST, encoding="utf-8", newline="\n")
    (pack / "ui" / "menus.ini").write_text(SKIN, encoding="utf-8", newline="\n")
    (mods / "mods.ini").write_text("[mods]\nui.demo = on\n", encoding="utf-8", newline="\n")
    return mods


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    ap.add_argument("--timeout", type=int, default=90)
    ap.add_argument("--only", choices=sorted(RECIPES), help="one screen only")
    a = ap.parse_args()
    mods = build_mod()
    folder = ROOT / "build" / "ui-skin-play"
    exe = prepare_folder(a.src, a.exe, folder)
    ok = True
    for variant in ("base", "mod"):
        for screen, recipe_text in RECIPES.items():
            if a.only and screen != a.only:
                continue
            out = OUT / f"{variant}_{screen}"
            out.mkdir(parents=True, exist_ok=True)
            for stale in out.glob("*.png"):
                stale.unlink()
            rec = out / "recipe.txt"
            rec.write_text(recipe_text, encoding="utf-8", newline="\n")
            extra = {"DRAKAN_MODS": str(mods) if variant == "mod" else str(OUT / "no-mods")}
            if screen == "title":
                extra["DRAKAN_START_LEVEL"] = ""
            env = game_env(out, folder, rec, a.timeout, extra)
            if screen == "title":
                env.pop("DRAKAN_START_LEVEL", None)
                env.pop("DRAKAN_GHOST", None)
                for k in ("DRAKAN_AUTOPRESS", "DRAKAN_AUTOPRESS_UNTIL"):
                    env.pop(k, None)  # the title menu must stay open
            with (out / "stdout_1.log").open("wb") as f:
                subprocess.run(
                    [str(exe)], cwd=folder, env=env, stdout=f, stderr=subprocess.STDOUT, timeout=a.timeout + 60
                )
            log = (out / "stdout_1.log").read_bytes().decode("latin-1")
            shots = sorted(p.name for p in out.glob("*.png"))
            skin_lines = [ln.strip() for ln in log.splitlines() if "[ui] skin" in ln]
            print(f"{variant}_{screen}: shots={shots} skin_log={skin_lines}")
            ok = ok and bool(shots) and (variant == "base" or bool(skin_lines))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
