"""Exercise every option of the engine's debug menu (docs/reference/debug-menu.md), one game run per page.

    python scripts/harness/debug_menu_sweep.py                      # all pages, PAL play folder
    python scripts/harness/debug_menu_sweep.py --pages 6,11 --exe dist/DrakanPC-usa/drakan.exe --name usa-sweep

Each run starts a New Game in Surdana (play_run.py), opens the menu with F3, turns to the page and, for every option,
takes two shots (the second gives the idle noise), presses Enter (the native "run": toggles a value, or runs a cheat /
command), takes a shot (and a RAM dump on the dump pages), presses Enter again and takes a last shot. Teleport, Suicide
and Quit are left out (see the reference doc for their own checks). With DRAKAN_DEBUG_MENU_LOG=1 the game logs every
shown value change; the report pairs them with the presses and measures the picture change outside the menu panel.
Output: build/harness/<name>-pNN/ (runs) and build/harness/<name>/report.json + page montages.
"""

from __future__ import annotations

import argparse
import json
import re
import struct
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PLAY_RUN = ROOT / "scripts/harness/play_run.py"

# Option rows per page (both discs); the special options are skipped by the sweep.
ROWS = [19, 12, 8, 6, 7, 18, 12, 9, 4, 6, 17]
SPECIAL = {(9, 0): "Teleport", (11, 13): "Suicide", (11, 14): "Quit"}
DUMP_PAGES = {11}
ENV = [
    "DRAKAN_DEBUG_MENU=1",
    "DRAKAN_DEBUG_MENU_LOG=1",
    "DRAKAN_FULLSCREEN=0",
    "DRAKAN_WINDOW_WIDTH=1280",
    "DRAKAN_WINDOW_HEIGHT=960",
    "DRAKAN_WS_ASPECT=4:3",
]


def recipe(page: int) -> str:
    lines = ["clock milliseconds", "0 sync play 60", "500 key tap F3"]
    t = 1000
    for _ in range(page - 1):
        lines.append(f"{t} key tap PGDN")
        t += 300
    t += 500
    for row in range(ROWS[page - 1]):
        label = f"p{page:02d}r{row:02d}"
        if (page, row) not in SPECIAL:
            lines.append(f"{t} shot {label}_a")
            t += 400
            lines.append(f"{t} shot {label}_n")
            if page in DUMP_PAGES:
                lines.append(f"{t} dump ram_{label}_a")
            t += 300
            lines.append(f"{t} key tap ENTER")
            t += 1100
            lines.append(f"{t} shot {label}_b")
            if page in DUMP_PAGES:
                lines.append(f"{t} dump ram_{label}_b")
            t += 200
            lines.append(f"{t} key tap ENTER")
            t += 1100
            lines.append(f"{t} shot {label}_c")
            t += 200
        lines.append(f"{t} key tap DOWN")
        t += 300
    lines += [f"{t} key tap F3", f"{t + 600} quit"]
    return "\n".join(lines) + "\n"


def run_page(name: str, page: int, exe: Path, timeout: int) -> Path:
    out = ROOT / "build/harness" / f"{name}-p{page:02d}"
    rec = ROOT / "build/harness" / f"{name}-p{page:02d}.txt"
    rec.parent.mkdir(parents=True, exist_ok=True)
    rec.write_text(recipe(page), encoding="utf-8", newline="\n")
    cmd = [sys.executable, str(PLAY_RUN), "--name", out.name, "--exe", str(exe), "--recipe", str(rec)]
    cmd += ["--attempts", "1", "--timeout", str(timeout)]
    for e in ENV:
        cmd += ["--env", e]
    res = subprocess.run(cmd, cwd=ROOT, capture_output=True, text=True)
    print(f"page {page}: exit {res.returncode} {res.stdout.strip().splitlines()[-1:] or res.stderr[-300:]}")
    return out


def scene_diff(out: Path, a: str, b: str) -> float | None:
    from PIL import Image, ImageChops, ImageStat

    pa, pb = out / f"{a}.png", out / f"{b}.png"
    if not (pa.exists() and pb.exists()):
        return None

    def crop(p):
        im = Image.open(p).convert("L")
        w, h = im.size  # the menu panel takes the top right; keep the left 60 % below the HUD text
        return im.crop((0, int(h * 0.12), int(w * 0.6), h)).resize((256, 300))

    return round(ImageStat.Stat(ImageChops.difference(crop(pa), crop(pb))).mean[0], 2)


def stats(path: Path) -> dict:
    """Rynn's position (controlled actor, ctx+0x4b0) and the encoded stats block (gameui/Addresses.h)."""
    if not path.exists():
        return {}
    data = path.read_bytes()

    def u(a):
        a &= 0x1FFFFFF
        return int.from_bytes(data[a : a + 4], "little")

    def f3(a):
        a &= 0x1FFFFFF
        return [round(v, 1) for v in struct.unpack_from("<3f", data, a)]

    key = u(0x4FE86C)

    def dec(off):
        x = u(0x4FBAC8 + off) ^ key
        x = (x >> 8 | x << 24) & 0xFFFFFFFF
        return x - (1 << 32) if x & 0x80000000 else x

    ctx = u(0x5565D4)
    out = {"position": f3(u(ctx + 0x4B0) + 0x14), "camera": f3(u(ctx + 0x4B4) + 0x14)}
    for name, off in (("hp", 0xC), ("hp_max", 0x10), ("gold", 0x18), ("mana", 0x1C), ("mana_max", 0x20), ("xp", 0x24)):
        out[name] = dec(off)
    out.update(level=dec(0x28), skill_points=dec(0x2C))
    return out


def evaluate(name: str, page: int, out: Path) -> list[dict]:
    log = (out / "drakan.log").read_text(errors="replace") if (out / "drakan.log").exists() else ""
    events = []  # (kind, text) in log order
    for line in log.splitlines():
        if m := re.search(r"\[in\] screenshot (\S+) ->", line):
            events.append(("shot", m.group(1)))
        elif m := re.search(r"\[debug-menu\] value page (\d+) row (\d+) (.*): (.*) -> (.*)$", line):
            events.append(("value", m.group(3) + ": " + m.group(4) + " -> " + m.group(5)))
    rows = []
    for row in range(ROWS[page - 1]):
        label = f"p{page:02d}r{row:02d}"
        if (page, row) in SPECIAL:
            rows.append({"label": label, "special": SPECIAL[(page, row)]})
            continue
        shots = [e[1] for e in events if e[0] == "shot"]

        def between(s, t):
            out_vals, on = [], False
            for kind, text in events:
                if kind == "shot" and text == s:
                    on = True
                elif kind == "shot" and text == t:
                    break
                elif on and kind == "value":
                    out_vals.append(text)
            return out_vals

        rec = {
            "label": label,
            "reached": f"{label}_c" in shots,
            "first": between(f"{label}_n", f"{label}_b"),
            "second": between(f"{label}_b", f"{label}_c"),
            "noise": scene_diff(out, f"{label}_a", f"{label}_n"),
            "change": scene_diff(out, f"{label}_n", f"{label}_b"),
            "restored": scene_diff(out, f"{label}_n", f"{label}_c"),
        }
        if page in DUMP_PAGES:
            rec["before"] = stats(out / f"ram_{label}_a.ram")
            rec["after"] = stats(out / f"ram_{label}_b.ram")
        rows.append(rec)
    return rows


def montage(out: Path, page: int, dest: Path) -> None:
    from PIL import Image

    tiles = []
    for row in range(ROWS[page - 1]):
        label = f"p{page:02d}r{row:02d}"
        ims = [out / f"{label}_{s}.png" for s in "nbc"]
        if all(p.exists() for p in ims):
            tiles.append([Image.open(p).convert("RGB").resize((320, 240)) for p in ims])
    if not tiles:
        return
    sheet = Image.new("RGB", (960, 240 * len(tiles)))
    for i, t in enumerate(tiles):
        for j, im in enumerate(t):
            sheet.paste(im, (j * 320, i * 240))
    sheet.save(dest)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--exe", type=Path, default=ROOT / "dist/DrakanPC/drakan.exe")
    ap.add_argument("--pages", default=",".join(str(p) for p in range(1, 12)))
    ap.add_argument("--name", default="debug-sweep")
    ap.add_argument("--timeout", type=int, default=240)
    ap.add_argument("--evaluate-only", action="store_true", help="re-read existing runs")
    a = ap.parse_args()
    dest = ROOT / "build/harness" / a.name
    dest.mkdir(parents=True, exist_ok=True)
    report = {}
    for page in (int(p) for p in a.pages.split(",")):
        out = ROOT / "build/harness" / f"{a.name}-p{page:02d}"
        if not a.evaluate_only:
            out = run_page(a.name, page, a.exe.resolve(), a.timeout)
        report[page] = evaluate(a.name, page, out)
        montage(out, page, dest / f"page{page:02d}.png")
    (dest / "report.json").write_text(json.dumps(report, indent=1) + "\n", encoding="utf-8")
    print(dest / "report.json")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
