"""Read the PS2 template files the Order of the Flame converter needs from the player's own Drakan 2 disc image.

`editor/scripts/ootf-convert.ts --ps2 <data dir>` wants the template level's LEVEL*.LVL / *.ODB files, the splat files, the
resource banks, the host MOD / TXD files (`<data>/../hosts`) and the player's rig (`<data>/../player`). Nothing here is ever
committed; the output lives under build/.

    python -I analysis/scripts/ootf_ps2_data.py --iso <disc.iso> --out build/odbx/data [--template LEVEL1NA]
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))

HOST_FILES = {f"{s}.{e}" for s in ("LEVEL1ON", "WORLDCON", "FX", "MOUNTAON") for e in ("MOD", "TXD")}
BANK_FILES = ("PROCTEES.TXD", "RESOURES.MOD", "RESOURES.TXD", "RESOURES.SDB", "RESOURES.ADB", "NPCS.MOD", "NPCS.ADB")


def is_complete(data: Path, template: str) -> bool:
    """True when every file the converter reads is already in `data` (and its siblings hosts/ and player/)."""
    return all(
        path.is_file()
        for path in (
            data / f"{template}.LVL",
            data / f"{template[:6]}TS.TXD",
            data / "paths.json",
            data / "RESOURES.MOD",
            data / "PROCTEES.TXD",
            data / "RESOURES.SDB",
            data.parent / "hosts" / "LEVEL1ON.MOD",
            data.parent / "player" / "SYSTEM.ADB",
        )
    )


def extract(iso: Path, data: Path, template: str = "LEVEL1NA") -> None:
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
        if n.endswith((".ODB", ".LVL", ".SSD", ".XDB")) or n in BANK_FILES or (in_template and n.startswith(stem)):
            (data / n).write_bytes(read(f, files[p]))
        if n in HOST_FILES and "/DESERTON/" not in p.upper():
            # own MOD / TXD of template ODBs that host models
            (data.parent / "hosts").mkdir(parents=True, exist_ok=True)
            (data.parent / "hosts" / n).write_bytes(read(f, files[p]))
        if n in ("SYSTEM.MOD", "SYSTEM.ADB"):  # the player's rig and clips (ootf-convert.ts --player)
            (data.parent / "player").mkdir(parents=True, exist_ok=True)
            (data.parent / "player" / n).write_bytes(read(f, files[p]))
        if n.endswith(".ODB"):  # the ODB's .DB (dependency list) and its disc directory, for the converter's model refs
            db = p[: -len(".ODB")] + ".DB"
            if db in files:
                (data / (n[:-4] + ".DB")).write_bytes(read(f, files[db]))
                dirs[n[:-4]] = "/".join(parts[:-1])
    (data / "paths.json").write_text(json.dumps(dirs), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--template", default="LEVEL1NA")
    args = parser.parse_args()
    if not args.iso.is_file():
        parser.error(f"ISO not found: {args.iso}")
    extract(args.iso, args.out, args.template)
    if not is_complete(args.out, args.template):
        print(
            "The disc image lacks files the converter needs (is it the Drakan: The Ancients' Gates disc?)",
            file=sys.stderr,
        )
        return 1
    print(f"PS2 template data: {args.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
