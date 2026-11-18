"""Install locally verified Order of the Flame save migrations into a play folder."""

import argparse
import hashlib
import importlib.util
import re
import tempfile
from pathlib import Path

MAX_SAVE_BYTES = 32 * 1024 * 1024
MAX_INDEX_BYTES = 256 * 1024


def read_catalog(directory: Path) -> tuple[dict[tuple[str, ...], str], dict[str, bytes]]:
    rows: dict[tuple[str, ...], str] = {}
    assets: dict[str, bytes] = {}
    index = directory / "index.tsv"
    with index.open("rb") as file:
        contents = file.read(MAX_INDEX_BYTES + 1)
    if len(contents) > MAX_INDEX_BYTES:
        raise ValueError(f"Migration index exceeds the game's index size limit: {index}")
    for number, line in enumerate(contents.decode("utf-8").splitlines(), 1):
        fields = line.split("\t")
        if len(fields) != 8:
            raise ValueError(f"{index}:{number} must contain eight tab-separated fields")
        source, target, filename, source_digest, replacement_digest, asset, companion_digest, companion = fields
        if (
            any(re.fullmatch(r"[0-9a-f]{16}", build) is None for build in (source, target))
            or source == target
            or re.fullmatch(r'[^\\/:*?"<>|\x00-\x1f]+\.r(?:sg|lt)', filename) is None
            or re.fullmatch(r"[0-9a-f]{64}", source_digest) is None
            or ((companion or companion_digest) and not filename.endswith(".rsg"))
        ):
            raise ValueError(f"{index}:{number} has an invalid source, target or save fingerprint")
        replacements = [(asset, replacement_digest)]
        if companion or companion_digest:
            replacements.append((companion, companion_digest))
        for name, digest in replacements:
            if re.fullmatch(r"[0-9a-f]{64}", digest) is None or name != f"{digest}.bin":
                raise ValueError(f"{index}:{number} has an invalid replacement fingerprint or filename")
            if name not in assets:
                path = directory / name
                with path.open("rb") as file:
                    payload = file.read(MAX_SAVE_BYTES + 1)
                if len(payload) > MAX_SAVE_BYTES:
                    raise ValueError(f"Replacement save exceeds the game's migration size limit: {path}")
                if not payload.startswith(b"SRSC") or hashlib.sha256(payload).hexdigest() != digest:
                    raise ValueError(f"Replacement save does not match its verification data: {path}")
                assets[name] = payload
        key = tuple(fields[:4])
        if rows.setdefault(key, line) != line:
            raise ValueError(f"{index}:{number} gives conflicting replacements for one save")
    if not rows:
        raise ValueError(f"No verified migrations in {index}")
    return rows, assets


def write_atomically(path: Path, data: bytes) -> None:
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=path.parent, prefix="migration-", suffix=".tmp", delete=False) as out:
            temporary = Path(out.name)
            out.write(data)
        temporary.replace(path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)


def install(source: Path, play: Path) -> None:
    rows, assets = read_catalog(source)
    # The installer's bundled Python leaves sibling scripts off sys.path.
    spec = importlib.util.spec_from_file_location(
        "install_private_mods", Path(__file__).with_name("install_private_mods.py")
    )
    tools = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tools)
    current_build, _ = tools.ootf_save_stamp(play / "mods" / "ootf-levels.zip")
    if any(target != current_build for _, target, _, _ in rows):
        raise ValueError(f"Verified migrations must match installed Order of the Flame build {current_build}")
    destination = play / "save-migrations" / "ootf"
    index = destination / "index.tsv"
    if index.exists():
        installed_rows, installed_assets = read_catalog(destination)
        for key, line in installed_rows.items():
            if rows.setdefault(key, line) != line:
                raise ValueError(f"A different migration is already installed for {key[2]}")
        for name in installed_assets:
            assets.pop(name, None)
    contents = ("\n".join(rows[key] for key in sorted(rows)) + "\n").encode("utf-8")
    if len(contents) > MAX_INDEX_BYTES:
        raise ValueError("Combined migration catalog exceeds the game's index size limit")
    destination.mkdir(parents=True, exist_ok=True)
    for name, data in assets.items():
        write_atomically(destination / name, data)
    # Publish the index last so the game only observes sets with every referenced payload in place.
    write_atomically(index, contents)
    print(f"Installed {len(rows)} verified Order of the Flame save migrations")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--play", required=True, type=Path)
    args = parser.parse_args()
    try:
        install(args.source, args.play)
    except (OSError, ValueError) as error:
        parser.exit(1, f"Could not install Order of the Flame save migrations: {error}\n")
