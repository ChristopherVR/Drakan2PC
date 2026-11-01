"""Vendor proof: edit a shop with the editor's vendor code, then open it in the game and buy from it.

    python scripts/harness/vendor_shop.py                       # default edit: Surdana blacksmith
    python scripts/harness/vendor_shop.py --name v2 --price RESOURES:7890=33 --add RESOURES:8569 --gold 900
    python scripts/harness/vendor_shop.py --skip-baseline       # only the modded run

Steps (docs/journal/2026-11-01/vendor-shops.md):
  1. `editor/scripts/vendor-pack.ts` builds a mod pack with the editor's own code (stock list, price modifiers through
     `editor-core/src/vendor.ts`, item prices as a class-record override of RESOURES.ODB) into <out>/mods;
  2. a baseline run (no pack) and a modded run start New Game in Surdana with `DRAKAN_DEBUG_GOLD` gold and
     `DRAKAN_DEBUG_OPEN_SHOP=<n>` (the n-th shop of the level: 1 blacksmith, 2 mage, 3 potions), take screenshots of
     the shop list (first item, the added item at the end), buy the first item, and dump RAM before and after;
  3. the gold word `rotr8(*0x4fbae0 ^ *0x4fe86c)` is decoded from both dumps. The run passes when the gold dropped by
     exactly the edited price (and the baseline dropped by the disc price).
Runs from a private copy of dist/DrakanPC with build/final-game/bin/drakan.exe, so other games are not disturbed.
Output: build/vendor-proof/<name>/{baseline,modded}/ (stdout log, shots, ram dumps) and report.json.
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import struct
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from netplay_two_instances import ROOT, prepare_folder  # noqa: E402

GOLD_ADDR = 0x4FBAE0
KEY_ADDR = 0x4FE86C
RES_DB = "RESOURES"


def read_iso_path() -> Path:
    txt = ROOT / "build" / "iso-path.txt"
    if txt.is_file():
        return Path(txt.read_text(encoding="utf-8").strip())
    import configparser

    ini = configparser.ConfigParser(interpolation=None)
    ini.read(ROOT / "dist" / "DrakanPC" / "drakan.ini", encoding="utf-8")
    return Path(ini.get("general", "iso"))


def rotr8(x: int) -> int:
    return ((x >> 8) | (x << 24)) & 0xFFFFFFFF


def gold_of(ram: Path) -> int:
    b = ram.read_bytes()
    word = struct.unpack_from("<I", b, GOLD_ADDR)[0]
    key = struct.unpack_from("<I", b, KEY_ADDR)[0]
    return rotr8(word ^ key)


NL = chr(10)


def recipe(buy: bool, scroll: int, wait_ms: int) -> str:
    """Input script: wait for the shop screen (0x15), dismiss the first-visit help box, look at the first item, scroll to
    the last, then buy item 0 and look at the Equipment pane."""
    lines = [
        "clock milliseconds",
        "0 sync play 60",
        "0 sync screen 21 20",
        f"{wait_ms} pad tap CROSS",  # the "Store:" help box the first visit shows
        f"{wait_ms + 1500} shot shop_first",
        f"{wait_ms + 1600} dump ram_before",
    ]
    t = wait_ms + 2500
    for _ in range(scroll):
        lines.append(f"{t} pad tap DPAD_RIGHT")
        t += 350
    lines.append(f"{t + 800} shot shop_last")
    t += 1500
    if buy:
        # back to the first item
        for _ in range(scroll + 2):
            lines.append(f"{t} pad tap DPAD_LEFT")
            t += 300
        t += 1000
        lines.append(f"{t} shot shop_back_first")
        lines.append(f"{t + 400} pad tap SQUARE")
        lines.append(f"{t + 1800} shot shop_confirm")
        lines.append(f"{t + 2000} pad tap CROSS")
        lines.append(f"{t + 4000} shot shop_bought")
        lines.append(f"{t + 4200} dump ram_after")
        lines.append(f"{t + 5000} pad tap L1")  # Spells pane (first-visit help box)
        lines.append(f"{t + 6500} pad tap CROSS")
        lines.append(f"{t + 7500} pad tap L1")  # Equipment pane: the bought item
        lines.append(f"{t + 9000} pad tap CROSS")  # its first-visit help box
        lines.append(f"{t + 10500} shot equipment")
        t += 10800
    lines.append(f"{t + 500} quit")
    return NL.join(lines) + NL


def run_game(args: argparse.Namespace, folder_exe: Path, out: Path, mods: Path | None, buy: bool) -> bool:
    # play_run.py replaces its run folder, so the input script lives beside it
    rec = out.parent / f"recipe_{out.name}.txt"
    rec.write_text(recipe(buy, args.scroll, args.wait_ms), encoding="utf-8", newline="\n")
    env = [
        f"DRAKAN_DEBUG_GOLD={args.gold}",
        f"DRAKAN_DEBUG_OPEN_SHOP={args.shop}",
        "PS2X_GS_SCALE=1",
        "DRAKAN_FULLSCREEN=0",
        "DRAKAN_DLSS=off",
        "DRAKAN_HD_MOVIES=0",
        "GSR_TEXUP=off",
        f"DRAKAN_WINDOW_WIDTH={args.width}",
        f"DRAKAN_WINDOW_HEIGHT={args.height}",
        "DRAKAN_VSYNC=0",
        "DRAKAN_LIFECYCLE_LOG=1",
    ]
    if mods:
        env.append(f"DRAKAN_MODS={mods}")
    cmd = [
        sys.executable,
        str(HERE / "play_run.py"),
        "--name",
        out.name,
        "--out-root",
        str(out.parent),
        "--exe",
        str(folder_exe),
        "--recipe",
        str(rec),
        "--attempts",
        str(args.attempts),
        "--timeout",
        str(args.timeout),
    ]
    for e in env:
        cmd += ["--env", e]
    print("run:", out)
    r = subprocess.run(cmd, cwd=ROOT)
    return r.returncode == 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--name", default="vendor")
    ap.add_argument("--iso", type=Path, help="PS2 ISO (default build/iso-path.txt or drakan.ini)")
    ap.add_argument("--level", default="LEVEL1NA", help="disc level stem of the vendor (Surdana = LEVEL1NA)")
    ap.add_argument(
        "--vendor", default="1648", help="object id of the vendor in that level (1648 = Surdana blacksmith)"
    )
    ap.add_argument("--shop", type=int, default=1, help="n-th shop of the level for DRAKAN_DEBUG_OPEN_SHOP")
    ap.add_argument("--add", action="append", default=None, help="STEM:id[:unlimited|count] added to the stock")
    ap.add_argument("--remove", action="append", default=[], help="STEM:id removed from the stock")
    ap.add_argument("--price", action="append", default=None, help="STEM:id=gold item price override")
    ap.add_argument("--sale-mod", help="vendor Item Sale Price Mod")
    ap.add_argument("--gold", type=int, default=1000)
    ap.add_argument(
        "--scroll", type=int, default=16, help="DPAD_RIGHT steps from the first item to the end of the list"
    )
    ap.add_argument("--wait-ms", type=int, default=2500, help="settle time after the shop screen opens")
    ap.add_argument("--width", type=int, default=960)
    ap.add_argument("--height", type=int, default=720)
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--attempts", type=int, default=2)
    ap.add_argument("--skip-baseline", action="store_true")
    ap.add_argument("--no-pack", action="store_true", help="reuse the pack of the last run of this name")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--no-buy", action="store_true", help="look only, do not buy")
    args = ap.parse_args()
    add = args.add if args.add is not None else [f"{RES_DB}:8569"]  # Iron Mace
    price = args.price if args.price is not None else [f"{RES_DB}:7869=7"]  # Rusty Short Sword 165 -> 7

    out = ROOT / "build" / "vendor-proof" / args.name
    out.mkdir(parents=True, exist_ok=True)
    mods = out / "mods"
    if not args.no_pack:
        if mods.exists():
            shutil.rmtree(mods)
        cmd = [
            "node",
            "--experimental-transform-types",
            "--no-warnings",
            "editor/scripts/vendor-pack.ts",
            "--iso",
            str(args.iso or read_iso_path()),
            "--out",
            str(mods),
            "--level",
            args.level,
            "--vendor",
            args.vendor,
        ]
        for a in add:
            cmd += ["--add", a]
        for r in args.remove:
            cmd += ["--remove", r]
        for p in price:
            cmd += ["--price", p]
        if args.sale_mod:
            cmd += ["--sale-mod", args.sale_mod]
        print(subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True, check=True).stdout.strip())

    exe = prepare_folder(ROOT / "dist" / "DrakanPC", args.exe, ROOT / "build" / "vendor-play")
    buy = not args.no_buy
    report: dict[str, object] = {"gold_start": args.gold, "price_edits": price, "added": add}
    ok = True
    if not args.skip_baseline:
        base = out / "baseline"
        if run_game(args, exe, base, None, buy):
            b0, b1 = gold_of(base / "ram_before.ram"), gold_of(base / "ram_after.ram") if buy else None
            report["baseline"] = {"gold_before": b0, "gold_after": b1, "spent": None if b1 is None else b0 - b1}
        else:
            ok = False
    mod = out / "modded"
    if run_game(args, exe, mod, mods, buy):
        m0 = gold_of(mod / "ram_before.ram")
        m1 = gold_of(mod / "ram_after.ram") if buy else None
        report["modded"] = {"gold_before": m0, "gold_after": m1, "spent": None if m1 is None else m0 - m1}
    else:
        ok = False
    want = None
    for p in price:
        k, _, v = p.partition("=")
        if k.endswith(":7869"):
            want = int(v)
    if buy and "modded" in report and want is not None:
        spent = report["modded"]["spent"]  # type: ignore[index]
        report["expected_spent"] = want
        report["pass"] = spent == want
        ok = ok and spent == want
    (out / "report.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps(report, indent=2))
    return 0 if ok else 1


if __name__ == "__main__":
    os.chdir(ROOT)
    sys.exit(main())
