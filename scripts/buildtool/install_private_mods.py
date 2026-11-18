"""Install private packs without replacing unrelated mod activation settings."""

import argparse
import configparser
import shutil
import tempfile
import tomllib
import zipfile
from pathlib import Path


def ootf_save_stamp(path: Path) -> tuple[str, int]:
    try:
        with zipfile.ZipFile(path) as archive:
            manifest = tomllib.loads(archive.read("mod.toml").decode("utf-8"))
    except (OSError, KeyError, ValueError, zipfile.BadZipFile) as error:
        raise ValueError(f"Could not verify Order of the Flame save compatibility for {path}") from error
    build = manifest.get("build")
    revision = manifest.get("save_revision", 0)
    if manifest.get("id") != "ootf-levels" or not isinstance(build, str) or not build or type(revision) is not int:
        raise ValueError(f"Missing Order of the Flame save build or revision in {path}")
    return build, revision


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
    copies = [(pack, target / pack.name) for pack in packs if pack.resolve() != (target / pack.name).resolve()]
    for pack, destination in copies:
        if pack.name != "ootf-levels.zip" or not destination.is_file():
            continue
        old_build, old_revision = ootf_save_stamp(destination)
        build, revision = ootf_save_stamp(pack)
        if build != old_build or revision < old_revision:
            raise ValueError(
                f"The installed Order of the Flame pack was kept. Build {build} revision {revision} cannot replace "
                f"build {old_build} revision {old_revision} without risking old saves. Reconvert against the play folder."
            )
    for pack, destination in copies:
        if pack.name != "ootf-levels.zip":
            shutil.copy2(pack, destination)
            continue
        with tempfile.NamedTemporaryFile(dir=target, prefix="ootf-levels-", suffix=".tmp", delete=False) as file:
            temporary = Path(file.name)
        try:
            shutil.copy2(pack, temporary)
            temporary.replace(destination)
        finally:
            temporary.unlink(missing_ok=True)
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
