"""Convert an Order of the Flame level, install it as a private mod pack and play it in the real game with screenshots.

    python scripts/harness/ootf_level_run.py --ootf build/ootf/cab_disc "Multiplayer/Ground/Relic/Relic.lvl"
    python scripts/harness/ootf_level_run.py --ootf build/ootf/cab_disc "Mountain World/Intro Level/Intro.lvl" --name intro

Steps (details: docs/journal/2026-10-31/ootf-ingame.md):
  1. extracts the template's LEVEL*.LVL / *.ODB and its splat files (LEVEL1TS.DB/.TXD) from the player's ISO (drakan.ini
     [general] iso, or --iso) into build/odbx/data once (needed by editor/scripts/ootf-convert.ts);
  2. runs the converter into build/ootf-run/<name>/mods/ootf-levels.zip with a mods.ini that enables it
     (DRAKAN_MODS points there, dist/DrakanPC is not touched);
  3. runs scripts/harness/play_run.py (New Game on an empty private card; the level replaces Level 1A - Surdana, so the
     intro cut scene plays first, about 20 s) with a recipe that stands, walks forward twice and takes screenshots.
     The window is small and DLSS/HD movies are off to keep the run fast.
     --campaign: the pack is a campaign (mod.toml campaign = "ootf") and the run uses DRAKAN_GAME=d1, so New Game opens
     the level at once, with no Drakan 2 intro (docs/reference/modding.md "Campaign packs").
Output: build/ootf-run/<name>/run/ (stdout_1.log, shots s_start / s_walk1 / s_walk2). Exit 0 when the guest reached
gameplay in the converted level and did not jump to a null target.
"""

from __future__ import annotations

import argparse
import configparser
import json
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "analysis" / "scripts"))

RECIPE = """clock milliseconds
0 sync play 60
2500 shot s_start
3000 key down W
6000 key up W
6300 shot s_walk1
6500 key down W
8000 key up W
8300 shot s_walk2
9000 quit
"""


def iso_path(arg: str | None) -> Path:
    if arg:
        return Path(arg)
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(ROOT / "dist" / "DrakanPC" / "drakan.ini", encoding="utf-8")
    return Path(ini.get("general", "iso"))


HOST_FILES = {f"{s}.{e}" for s in ("LEVEL1ON", "WORLDCON", "FX", "MOUNTAON") for e in ("MOD", "TXD")}


def extract(iso: Path, data: Path, template: str) -> None:
    from ui_layout_dump import iso_files, read

    f, files = iso_files(str(iso))
    data.mkdir(parents=True, exist_ok=True)
    stem = template[:6] + "TS"
    dirs: dict[str, str] = {}
    for p in sorted(files):
        parts = p.strip("/").split("/")
        if parts[0].upper() != "ENGLISH":
            continue
        n = parts[-1].upper()
        in_template = len(parts) > 2 and parts[-2].upper() == template
        if (
            n.endswith((".ODB", ".LVL", ".SSD", ".XDB"))
            or n
            in ("PROCTEES.TXD", "RESOURES.MOD", "RESOURES.TXD", "RESOURES.SDB", "RESOURES.ADB", "NPCS.MOD", "NPCS.ADB")
            or (in_template and n.startswith(stem))
        ):
            (data / n).write_bytes(read(f, files[p]))
        if n in HOST_FILES and not any(
            x in p.upper() for x in ("/DESERTON/",)
        ):  # own MOD / TXD of template ODBs that host models
            (data.parent / "hosts").mkdir(parents=True, exist_ok=True)
            (data.parent / "hosts" / n).write_bytes(read(f, files[p]))
        if n in ("SYSTEM.MOD", "SYSTEM.ADB"):  # the player's rig and clips (ootf-convert.ts --player)
            (data.parent / "player").mkdir(parents=True, exist_ok=True)
            (data.parent / "player" / n).write_bytes(read(f, files[p]))
        if n.endswith(".ODB"):  # the ODB's .DB (dependency list) and its disc directory, for the converter's model refs
            db = p[: -len(".ODB")] + ".DB" if p.upper().endswith(".ODB") else None
            if db in files:
                (data / (n[:-4] + ".DB")).write_bytes(read(f, files[db]))
                dirs[n[:-4]] = "/".join(parts[:-1])
    (data / "paths.json").write_text(json.dumps(dirs), encoding="utf-8")


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument(
        "level", help='PC level path relative to the OotF install, e.g. "Multiplayer/Ground/Relic/Relic.lvl"'
    )
    ap.add_argument(
        "--ootf", type=Path, default=ROOT / "build" / "ootf" / "cab_disc", help="OotF install / disc folder"
    )
    ap.add_argument("--iso", help="PS2 ISO (default: drakan.ini [general] iso)")
    ap.add_argument("--template", default="LEVEL1NA", help="PS2 template level (LEVEL1NA = the one verified in game)")
    ap.add_argument("--name", default="ootf", help="run name under build/ootf-run/")
    ap.add_argument("--timeout", type=int, default=150)
    ap.add_argument("--recipe", type=Path, help="own input script (must end in quit)")
    ap.add_argument("--no-convert", action="store_true", help="reuse the pack from the last run of this name")
    ap.add_argument(
        "--campaign",
        action="store_true",
        help='pack the levels as a campaign (mod.toml campaign = "ootf") and boot with DRAKAN_GAME=d1: New Game opens '
        "the first level directly (no Drakan 2 intro); without it the level replaces Level 1A after the D2 intro",
    )
    ap.add_argument("--env", action="append", default=[])
    ap.add_argument(
        "--exe", type=Path, help="game executable (default dist/DrakanPC/drakan.exe); lets a run use its own build"
    )
    ap.add_argument(
        "--convert-arg", action="append", default=[], help="extra converter argument, e.g. --only (repeat per token)"
    )
    a = ap.parse_args()

    work = ROOT / "build" / "ootf-run" / a.name
    mods = work / "mods"
    data = ROOT / "build" / "odbx" / "data"
    if (
        not (data / f"{a.template}.LVL").is_file()
        or not (data / f"{a.template[:6]}TS.TXD").is_file()
        or not (data / "paths.json").is_file()
        or not (data / "RESOURES.MOD").is_file()
        or not (data / "PROCTEES.TXD").is_file()
        or not (data / "RESOURES.SDB").is_file()
        or not (data.parent / "hosts" / "LEVEL1ON.MOD").is_file()
        or not (data.parent / "player" / "SYSTEM.ADB").is_file()
    ):
        extract(iso_path(a.iso), data, a.template)
    zip_path = mods / "ootf-levels.zip"
    if not a.no_convert:
        mods.mkdir(parents=True, exist_ok=True)
        cmd = [
            "node", "--experimental-transform-types", "--no-warnings", "editor/scripts/ootf-convert.ts",
            "--ootf", str(a.ootf), "--ps2", str(data), "--template", a.template, "--out", str(zip_path), *a.convert_arg, a.level,
        ]  # fmt: skip
        if a.campaign:
            cmd[cmd.index(a.level) : cmd.index(a.level)] = ["--campaign", "1"]
        out = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True)
        print(out.stdout.strip())
        (mods / "mods.ini").write_text("[mods]\nootf-levels = on\n", encoding="utf-8")
    import zipfile

    toml = zipfile.ZipFile(zip_path).read("mod.toml").decode()
    title = re.search(r'levels = \["([^"]*)"', toml).group(1)  # the first (start) level of a multi-level pack
    recipe = a.recipe or work / "recipe.txt"
    if not a.recipe:
        recipe.write_text(RECIPE, encoding="utf-8", newline="\n")
    run_cmd = [
        sys.executable, str(ROOT / "scripts" / "harness" / "play_run.py"),
        "--name", "run", "--out-root", str(work), "--recipe", str(recipe),
        "--timeout", str(a.timeout), "--attempts", "1", *(["--exe", str(a.exe)] if a.exe else []),
        "--env", f"DRAKAN_MODS={mods}",
        *(["--env", "DRAKAN_GAME=d1"] if a.campaign else ["--env", f"DRAKAN_START_LEVEL={title}"]),
        "--env", "PS2X_GS_SCALE=1", "--env", "DRAKAN_FULLSCREEN=0", "--env", "DRAKAN_DLSS=off",
        "--env", "DRAKAN_HD_MOVIES=0", "--env", "GSR_TEXUP=off", "--env", "DRAKAN_WINDOW_WIDTH=960",
        "--env", "DRAKAN_WINDOW_HEIGHT=540", "--env", "DRAKAN_VSYNC=0", "--env", "DRAKAN_WORLD=2", "--env", "DRAKAN_LIFECYCLE_LOG=1",
        *[x for e in a.env for x in ("--env", e)],
    ]  # fmt: skip
    subprocess.run(run_cmd, cwd=ROOT)
    log = work / "run" / "stdout_1.log"
    text = log.read_text(encoding="latin-1") if log.is_file() else ""
    mod_lines = [ln for ln in text.splitlines() if "mods:" in ln]
    print("\n".join(mod_lines))
    loaded = re.findall(r"afterLevelLoad level='(.*?)'", text)
    ents = re.findall(r"sample status Ok, (\d+) entities, level '(.*?)'", text)
    print("levels loaded:", loaded, "| world samples:", ents[-3:])
    ok = title in loaded and "missing-target" not in text and "[crash]" not in text and "[in] script quit" in text
    print("PASS" if ok else "FAIL", "-", work / "run")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
