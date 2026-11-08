"""Put the multiplayer map pictures into the mod packs that own the maps (docs/journal/2026-11-08/ootf-mp-map-pictures.md).

    python scripts/harness/pack_map_previews.py                       # every enabled pack in dist/DrakanPC/mods
    python scripts/harness/pack_map_previews.py --pack ootf-levels --pictures dist/DrakanPC/previews/maps

For each maps/*.ini [arena] of a pack, <pictures>/<safe map name>.png (made by map_previews.py) is added to the zip as
previews/<safe map name>.png, where the game finds a pack's pictures (it copies them to previews/mods/). mod.toml is
not touched, so a pinned `build` id (save compatibility) stays. Idempotent: a pack that already has the same pictures is
not rewritten; the zip is replaced atomically; a missing picture is skipped. Standard library only.
"""

from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from map_previews import arenas, safe_name

ROOT = Path(__file__).resolve().parents[2]


def add_previews(pack: Path, pictures: Path) -> list[str]:
    """Add the missing / changed pictures to the pack zip; returns the entry names written."""
    with zipfile.ZipFile(pack) as z:
        entries = {i.filename: z.read(i.filename) for i in z.infolist()}
        order = [i.filename for i in z.infolist()]
    want: dict[str, bytes] = {}
    for n, data in entries.items():
        if n.startswith("maps/") and n.lower().endswith(".ini"):
            for m in arenas(data.decode("utf-8", errors="replace")):
                png = pictures / (safe_name(m.get("name", m["id"])) + ".png")
                if png.is_file():
                    want[f"previews/{png.name}"] = png.read_bytes()
    changed = [n for n, d in want.items() if entries.get(n) != d]
    if not changed:
        return []
    tmp = pack.with_suffix(".zip.tmp")
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED) as out:
        for n in order:
            out.writestr(n, want.get(n, entries[n]))
        for n in changed:
            if n not in entries:
                out.writestr(n, want[n])
    tmp.replace(pack)
    return changed


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--mods", type=Path, default=ROOT / "dist" / "DrakanPC" / "mods")
    ap.add_argument("--pictures", type=Path, default=ROOT / "dist" / "DrakanPC" / "previews" / "maps")
    ap.add_argument("--pack", default="", help="pack ids (zip names without .zip), comma separated (default: all)")
    a = ap.parse_args()
    want = {s.strip().lower() for s in a.pack.split(",") if s.strip()}
    for pack in sorted(a.mods.glob("*.zip")):
        if want and pack.stem.lower() not in want:
            continue
        try:
            done = add_previews(pack, a.pictures)
        except (OSError, zipfile.BadZipFile) as e:
            print(f"{pack.name}: skipped ({e})")
            continue
        print(f"{pack.name}: " + (f"added {len(done)} picture(s)" if done else "pictures up to date or none to add"))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
