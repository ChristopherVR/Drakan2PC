"""Make the multiplayer menu's map pictures on the player's machine (docs/journal/2026-11-08/mp-map-pictures.md).

    python scripts/harness/map_previews.py                                  # every built-in arena without a picture
    python scripts/harness/map_previews.py --maps surdana-meadow --force    # one map again
    python scripts/harness/map_previews.py --out dist/DrakanPC/previews/maps --src dist/DrakanPC --exe <drakan.exe>

For each [arena] of configs/mp_maps.ini (and --maps-file, and the maps/*.ini of the enabled packs in <src>/mods, e.g. the
Order of the Flame levels; --no-mods skips those) one game instance loads the map's level in the arena world mode
(DRAKAN_ARENA_FORCE, no network session) and takes the authored picture (DRAKAN_ARENA_PREVIEW_SHOT: the player stands at
the map's `preview` point, the follow camera settles, the window is saved). The frame is checked (a black or flat frame
is a level that never came up, retried once), the HUD strip at the bottom is cut away, and the rest is centre-cropped to
16:9 and written as <out>/<map name>.png, the name the game looks for (mapSafeName). The game reads previews/maps/ after
the player's own previews/ and the mod packs' pictures. Nothing here is committed; the pictures are made from the
player's disc. Standard library only (the build's Python may not have Pillow).
"""

from __future__ import annotations

import argparse
import shutil
import struct
import subprocess
import sys
import time
import zipfile
import zlib
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netplay_two_instances import game_env, prepare_folder

ROOT = Path(__file__).resolve().parents[2]
WIDTH, HEIGHT = 1600, 900  # 16:9 window, so the world uses the full widescreen view
KEEP_TOP = 0.84  # the frame above the native HUD orbs (at HUD scale 0.5 they sit in the bottom ~14 %)


def arenas(text: str) -> list[dict[str, str]]:
    """The [arena <id>] sections of an mp_maps.ini: {id, name, level, ...} (first value of each key)."""
    found: list[dict[str, str]] = []
    cur: dict[str, str] | None = None
    for raw in text.splitlines():
        line = raw.split(";", 1)[0].strip()
        if line.startswith("["):
            cur = None
            if line.startswith("[arena"):
                cur = {"id": line[6:].strip(" ]")}
                found.append(cur)
        elif "=" in line and cur is not None:
            k, v = line.split("=", 1)
            cur.setdefault(k.strip().lower(), v.strip())
    return [a for a in found if a.get("id") and a.get("level")]


def mod_arenas(src: Path) -> list[dict[str, str]]:
    """The [arena] sections of the enabled mod packs (<src>/mods/*.zip, maps/*.ini; mods.ini lists `id = on|off`, a pack
    it does not list is on, like the game). The game loads the packs itself, so DRAKAN_ARENA_FORCE finds them by id."""
    mods = src / "mods"
    off: set[str] = set()
    ini = mods / "mods.ini"
    if ini.is_file():
        for raw in ini.read_text(encoding="utf-8", errors="replace").splitlines():
            k, _, v = raw.split(";", 1)[0].partition("=")
            if v.strip().lower() in ("off", "0", "false", "no"):
                off.add(k.strip().lower())
    found: list[dict[str, str]] = []
    for zp in sorted(mods.glob("*.zip")) if mods.is_dir() else []:
        if zp.stem.lower() in off:
            continue
        try:
            with zipfile.ZipFile(zp) as z:
                for n in sorted(z.namelist()):
                    if n.startswith("maps/") and n.lower().endswith(".ini"):
                        for m in arenas(z.read(n).decode("utf-8", errors="replace")):
                            m["pack"] = zp.name
                            found.append(m)
        except (OSError, zipfile.BadZipFile) as e:
            print(f"mod pack {zp.name}: unreadable ({e})")
    return found


def safe_name(name: str) -> str:
    """drakan_settings_host.cpp mapSafeName: letters, digits, '-' and '_' kept, anything else '_'."""
    return "".join(c if (c.isascii() and c.isalnum()) or c in "-_" else "_" for c in name)


def read_png(data: bytes) -> tuple[int, int, int, bytes]:
    """(width, height, channels, pixels) of an 8-bit, non-interlaced RGB / RGBA PNG."""
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("not a PNG")
    pos, idat, w = 8, [], 0
    h = ctype = 0
    while pos < len(data):
        (n,) = struct.unpack_from(">I", data, pos)
        tag = data[pos + 4 : pos + 8]
        body = data[pos + 8 : pos + 8 + n]
        pos += 12 + n
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
            if depth != 8 or ctype not in (2, 6) or interlace:
                raise ValueError("unsupported PNG layout")
        elif tag == b"IDAT":
            idat.append(body)
        elif tag == b"IEND":
            break
    bpp = 3 if ctype == 2 else 4
    raw = zlib.decompress(b"".join(idat))
    stride = w * bpp
    out = bytearray(stride * h)
    prev = bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1 : (y + 1) * (stride + 1)])
        if f == 1:
            for i in range(bpp, stride):
                line[i] = (line[i] + line[i - bpp]) & 0xFF
        elif f == 2:
            line = bytearray((a + b) & 0xFF for a, b in zip(line, prev))
        elif f == 3:
            for i in range(stride):
                left = line[i - bpp] if i >= bpp else 0
                line[i] = (line[i] + ((left + prev[i]) >> 1)) & 0xFF
        elif f == 4:
            for i in range(stride):
                a = line[i - bpp] if i >= bpp else 0
                b = prev[i]
                c = prev[i - bpp] if i >= bpp else 0
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 0xFF
        out[y * stride : (y + 1) * stride] = line
        prev = line
    return w, h, bpp, bytes(out)


def write_png(path: Path, w: int, h: int, rgb: bytes) -> None:
    raw = b"".join(b"\0" + rgb[y * w * 3 : (y + 1) * w * 3] for y in range(h))

    def chunk(tag: bytes, body: bytes) -> bytes:
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body) & 0xFFFFFFFF)

    path.write_bytes(
        b"\x89PNG\r\n\x1a\n"
        + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
        + chunk(b"IDAT", zlib.compress(raw, 6))
        + chunk(b"IEND", b"")
    )


def crop_box(w: int, h: int, keep_top: float = KEEP_TOP) -> tuple[int, int, int, int]:
    """(x0, y0, x1, y1): the frame above the HUD strip, centre-cropped to 16:9."""
    bh = max(1, int(h * keep_top))
    cw = min(w, bh * 16 // 9)
    ch = min(bh, cw * 9 // 16)
    x0 = (w - cw) // 2
    return x0, 0, x0 + cw, ch


def frame_ok(w: int, h: int, bpp: int, px: bytes) -> tuple[bool, str]:
    """A real picture of the level: not black (a level still loading) and not one flat colour (a fade)."""
    lum = []
    for y in range(0, h, max(1, h // 48)):
        row = y * w * bpp
        for x in range(0, w, max(1, w // 64)):
            i = row + x * bpp
            lum.append((px[i] * 299 + px[i + 1] * 587 + px[i + 2] * 114) // 1000)
    mean = sum(lum) / len(lum)
    spread = (sum((v - mean) ** 2 for v in lum) / len(lum)) ** 0.5
    if mean < 12:
        return False, f"black frame (mean {mean:.0f})"
    if spread < 8:
        return False, f"flat frame (spread {spread:.0f})"
    return True, f"mean {mean:.0f}, spread {spread:.0f}"


def make_picture(shot: Path, dest: Path) -> tuple[bool, str]:
    w, h, bpp, px = read_png(shot.read_bytes())
    x0, y0, x1, y1 = crop_box(w, h)
    rgb = bytearray()
    for y in range(y0, y1):
        row = px[(y * w + x0) * bpp : (y * w + x1) * bpp]
        rgb += row if bpp == 3 else bytes(b for i, b in enumerate(row) if i % 4 != 3)
    ok, why = frame_ok(x1 - x0, y1 - y0, 3, bytes(rgb))
    if ok:
        dest.parent.mkdir(parents=True, exist_ok=True)
        tmp = dest.with_suffix(".tmp")
        write_png(tmp, x1 - x0, y1 - y0, bytes(rgb))
        tmp.replace(dest)
    return ok, why


def capture(m: dict[str, str], a: argparse.Namespace, exe: Path) -> Path | None:
    """One game run: the window shot of the map's preview point, or None."""
    out = ROOT / "build" / "harness" / "map_previews" / m["id"]
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    shot = out / "preview.png"
    recipe = out / "recipe.txt"
    recipe.write_text(f"clock milliseconds\n0 sync play 60\n{a.timeout * 1000 - 5000} quit\n", encoding="utf-8")
    extra = {
        "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro",
        "DRAKAN_START_LEVEL": m["level"],
        "DRAKAN_ARENA_FORCE": m["id"],
        "DRAKAN_ARENA_PREVIEW_SHOT": str(shot),
        "DRAKAN_ARENA_LOG": "1",
        "DRAKAN_WINDOW_WIDTH": str(WIDTH),
        "DRAKAN_WINDOW_HEIGHT": str(HEIGHT),
        "DRAKAN_WS_ASPECT": "16:9",
        "DRAKAN_SHOW_FPS": "0",
        "DRAKAN_HUD_SCALE": "0.5",
        # the air arenas start on a launch pad high above the ground: without this Rynn dies in the fall and the
        # game returns to the title before the shot
        "DRAKAN_PLAYTEST_GOD": "1",
    }
    if a.maps_file:
        extra["DRAKAN_MP_MAPS"] = str(a.maps_file.resolve())
    env = game_env(out, exe.parent, recipe, a.timeout, extra)
    env.pop("DRAKAN_GHOST", None)
    log = out / "stdout_1.log"
    with log.open("wb") as f:
        p = subprocess.Popen([str(exe)], cwd=exe.parent, env=env, stdout=f, stderr=subprocess.STDOUT)
        t0 = time.monotonic()
        while p.poll() is None:
            time.sleep(1)
            text = log.read_bytes().decode("latin-1")
            if shot.is_file() and time.monotonic() - t0 > 5:
                time.sleep(2)  # the writer finishes the file
                break
            if "no picture" in text or time.monotonic() - t0 > a.timeout + 30:
                break
        if p.poll() is None:
            p.terminate()
            try:
                p.wait(10)
            except subprocess.TimeoutExpired:
                p.kill()
    for line in log.read_bytes().decode("latin-1").splitlines():
        if "preview shot" in line:
            print("   ", line.strip())
    return shot if shot.is_file() else None


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", type=Path, default=ROOT / "dist" / "DrakanPC" / "previews" / "maps")
    ap.add_argument("--maps", default="", help="arena ids or names, comma separated (default: all)")
    ap.add_argument("--maps-file", type=Path, default=None, help="an extra mp_maps.ini (DRAKAN_MP_MAPS)")
    ap.add_argument("--no-mods", action="store_true", help="skip the arenas of the enabled mod packs in <src>/mods")
    ap.add_argument("--only-mods", action="store_true", help="only the arenas of the enabled mod packs")
    ap.add_argument("--force", action="store_true", help="make the picture again even when one exists")
    ap.add_argument("--timeout", type=int, default=150, help="seconds per map")
    ap.add_argument("--exe", type=Path, default=ROOT / "build" / "final-game" / "bin" / "drakan.exe")
    ap.add_argument("--src", type=Path, default=ROOT / "dist" / "DrakanPC")
    a = ap.parse_args()

    maps = arenas((ROOT / "configs" / "mp_maps.ini").read_text(encoding="utf-8"))
    if a.maps_file:
        maps += arenas(a.maps_file.read_text(encoding="utf-8"))
    if a.only_mods:
        maps = []
    if not a.no_mods:
        have = {m["id"] for m in maps}
        maps += [m for m in mod_arenas(a.src) if m["id"] not in have]
    if a.maps:
        want = {s.strip().lower() for s in a.maps.split(",") if s.strip()}
        maps = [m for m in maps if m["id"].lower() in want or m.get("name", "").lower() in want]
    todo = [m for m in maps if a.force or not (a.out / (safe_name(m.get("name", m["id"])) + ".png")).is_file()]
    if not todo:
        print(f"map pictures: all {len(maps)} present in {a.out}")
        return 0
    exe = prepare_folder(a.src, a.exe, ROOT / "build" / "mapstudio")
    failed = []
    for m in todo:
        name = m.get("name", m["id"])
        dest = a.out / (safe_name(name) + ".png")
        print(f"map picture: {name} ({m['level']})", flush=True)
        for attempt in (1, 2):
            shot = capture(m, a, exe)
            ok, why = make_picture(shot, dest) if shot else (False, "no window shot")
            print(f"    attempt {attempt}: {'written ' + str(dest) if ok else 'rejected'} ({why})", flush=True)
            if ok:
                break
        else:
            failed.append(name)
    if failed:
        print("map pictures missing (the menu draws the sketch): " + ", ".join(failed))
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
