"""Make a private copy of the disc image whose ENGLISH tree is the GERMAN one (or another language).

Reproduces the issue 1 reporter's workaround (German files placed under the English path of the PS2 image) with a
consistent image: only the root directory record of ENGLISH is pointed at the other tree's extent, so every size and
sector stays valid. Use it to test language-independent behaviour (stutter, resource lookups) without a hand-edited ISO.

    python scripts/harness/swap_language_iso.py "<original.iso>" build/iss1/swapped.iso [--from GERMAN] [--onto ENGLISH]
    DRAKAN_ISO=build/iss1/swapped.iso DRAKAN_LANG=english build/iss1/play/drakan.exe

The copy is written under build/ (never commit it). Needs the free disk space of the image (about 3.4 GB).
"""

from __future__ import annotations

import argparse
import shutil
import struct
from pathlib import Path


def root_records(image, lba: int, size: int) -> dict[str, int]:
    image.seek(lba * 2048)
    data = image.read(size)
    records: dict[str, int] = {}
    pos = 0
    while pos < size:
        length = data[pos]
        if length == 0:
            pos = (pos // 2048 + 1) * 2048
            continue
        name_length = data[pos + 32]
        name = data[pos + 33 : pos + 33 + name_length].decode("latin1").split(";")[0].upper()
        records[name] = pos
        pos += length
    return records


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("source", type=Path)
    parser.add_argument("target", type=Path)
    parser.add_argument("--from", dest="donor", default="GERMAN", help="tree whose files are used (default GERMAN)")
    parser.add_argument("--onto", default="ENGLISH", help="tree that is replaced (default ENGLISH)")
    args = parser.parse_args()
    if args.target.resolve() == args.source.resolve():
        parser.error("the target must be a new file")
    shutil.copyfile(args.source, args.target)
    with args.target.open("r+b") as image:
        image.seek(16 * 2048)
        volume = image.read(2048)
        if volume[0] != 1 or volume[1:6] != b"CD001":
            parser.error("not an ISO9660 image")
        root = volume[156:190]
        lba, size = struct.unpack("<I", root[2:6])[0], struct.unpack("<I", root[10:14])[0]
        records = root_records(image, lba, size)
        donor, onto = args.donor.upper(), args.onto.upper()
        if donor not in records or onto not in records:
            parser.error(f"root has {sorted(records)}; need both {donor} and {onto}")
        image.seek(lba * 2048 + records[donor] + 2)
        extent = image.read(16)  # LBA and size, little and big endian
        image.seek(lba * 2048 + records[onto] + 2)
        image.write(extent)
    print(f"{args.target}: {onto} now uses the {donor} tree")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
