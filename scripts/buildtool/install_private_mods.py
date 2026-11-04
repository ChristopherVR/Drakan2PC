"""Install private packs without replacing unrelated mod activation settings."""

import argparse
import configparser
import shutil
from pathlib import Path


def install(source: Path, target: Path, activate: str | None = None) -> None:
    target.mkdir(parents=True, exist_ok=True)
    settings = configparser.ConfigParser(interpolation=None)
    settings.read(target / "mods.ini", encoding="utf-8-sig")
    if not settings.has_section("mods"):
        settings.add_section("mods")
    if source.is_dir():
        incoming = configparser.ConfigParser(interpolation=None)
        incoming.read(source / "mods.ini", encoding="utf-8-sig")
        packs = sorted(source.glob("*.zip"))
        if incoming.has_section("mods"):
            for key, value in incoming.items("mods"):
                settings.set("mods", key, value)
    else:
        if source.suffix.lower() != ".zip" or not source.is_file():
            raise ValueError(f"Pack missing: {source}")
        packs = [source]
    for pack in packs:
        destination = target / pack.name
        if pack.resolve() != destination.resolve():
            shutil.copy2(pack, destination)
    if activate:
        settings.set("mods", activate, "on")
    with (target / "mods.ini").open("w", encoding="utf-8", newline="\n") as out:
        settings.write(out)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--target", required=True, type=Path)
    parser.add_argument("--activate")
    args = parser.parse_args()
    install(args.source, args.target, args.activate)
