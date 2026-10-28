"""Incrementally stage an optional shared HD pack through the canonical builder."""

import argparse
import json
import re
import shutil
import stat
from pathlib import Path


def stage(source, destination):
    source = source.resolve(strict=True)
    destination = destination.resolve()
    if source == destination:
        return 0
    destination.mkdir(parents=True, exist_ok=True)
    manifest = destination / ".drakan-pack-stage.json"
    try:
        previous_sources = json.loads(manifest.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        previous_sources = {}
    imported = {}
    copied = 0
    for texture in source.iterdir():
        if not re.fullmatch(r"(?:[0-9a-f]{16}\.(?:png|dds)|alias\.txt|exclude\.txt)", texture.name, re.IGNORECASE):
            continue
        target = destination / texture.name
        original = texture.stat()
        if not stat.S_ISREG(original.st_mode):
            continue
        try:
            previous = target.stat()
        except FileNotFoundError:
            previous = None
        stamp = [str(source), original.st_size, original.st_mtime_ns]
        recorded = previous_sources.get(texture.name)
        # DrvFS may preserve only whole seconds on copy2. Remember the source's
        # exact timestamp rather than recopying the entire pack on every build.
        changed = recorded != stamp if recorded else previous and int(original.st_mtime) > int(previous.st_mtime)
        if previous is None or original.st_size != previous.st_size or changed:
            shutil.copy2(texture, target)
            copied += 1
        imported[texture.name] = stamp
    manifest.write_text(json.dumps(imported, sort_keys=True) + "\n", encoding="utf-8")
    return copied


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    print(f"Staged {stage(args.source, args.destination)} new/updated HD pack files")


if __name__ == "__main__":
    main()
