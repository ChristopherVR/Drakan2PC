"""Generate a private multiplayer character motion pack from the player's PAL disc (no assets are tracked)."""

import argparse
import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

import coop_character_roster as roster
import lvl_dump
import odb_fields
from ui_layout_dump import iso_files, read


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--iso", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    source_files = [Path(__file__), Path(__file__).with_name("character_motion_retarget.ts")]
    source_files += list((root / "editor/packages/formats/src").glob("*.ts"))
    fingerprint = hashlib.sha256()
    for source in source_files:
        fingerprint.update(source.read_bytes())
    fingerprint.update(str((args.iso.stat().st_size, args.iso.stat().st_mtime_ns)).encode())
    signature = fingerprint.hexdigest()
    if args.out.is_file():
        with zipfile.ZipFile(args.out) as existing:
            if "signature.txt" in existing.namelist() and existing.read("signature.txt").decode() == signature:
                print("Character motions cached:", args.out)
                return
    node = shutil.which("node")
    if not node:
        raise RuntimeError("Node.js 22.7+ is required to generate compatible character animations")
    args.out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="character-motion-", dir=args.out.parent) as temporary:
        scratch = Path(temporary)
        output = scratch / "pack"
        output.mkdir()
        disc, files = iso_files(str(args.iso))
        try:
            get = lambda stem, extension: read(
                disc, files[f"/ENGLISH/COMMON/{'SYSTEM' if stem == 'SYSTEM' else 'NPC'}/{stem}.{extension}"]
            )
            system = odb_fields.load_classes(get("SYSTEM", "ODB"))
            npcs = odb_fields.load_classes(get("NPCS", "ODB"))
            models = sorted({npcs[cid]["model"][0] for cid in roster.CHARACTER_IDS if cid})
            clips = sorted(roster.rynn_clip_activities(system[roster.RYNN_CLASS]))
            if len(clips) > 256 or len(models) > 32:
                raise ValueError("generated animation id allocation exceeded")
            model_records = []
            for stem, ids in [("SYSTEM", [3877]), ("NPCS", models)]:
                blob = get(stem, "MOD")
                for entry in lvl_dump.parse_srsc(blob):
                    if entry.type == 0x208 and entry.id in ids:
                        (scratch / f"{entry.id}.skel").write_bytes(blob[entry.offset : entry.offset + entry.size])
                        if stem == "NPCS":
                            model_records.append({"id": entry.id, "sub": entry.sub})
            model_records.sort(key=lambda row: row["id"])
            blob = get("SYSTEM", "ADB")
            for entry in lvl_dump.parse_srsc(blob):
                if entry.id in clips and entry.type in (0x501, 0x502):
                    suffix = "info" if entry.type == 0x501 else "data"
                    (scratch / f"{entry.id}.{suffix}").write_bytes(blob[entry.offset : entry.offset + entry.size])
            taken = sorted({entry.id for entry in lvl_dump.parse_srsc(get("NPCS", "ADB"))})
        finally:
            disc.close()
        (scratch / "input.json").write_text(
            json.dumps({"models": model_records, "clips": [{"id": clip} for clip in clips], "taken": taken})
        )
        subprocess.run(
            [node, "--experimental-transform-types", str(source_files[1]), str(scratch), str(output)],
            cwd=root,
            check=True,
        )
        (output / "mod.toml").write_text(
            'id = "drakan-character-motion"\nname = "Original multiplayer character motions"\nversion = "1.0.0"\n'
            '[target]\nprofile = "pal"\n',
            newline="\n",
        )
        (output / "signature.txt").write_text(signature)
        replacement = args.out.with_suffix(".tmp")
        with zipfile.ZipFile(replacement, "w", zipfile.ZIP_DEFLATED) as archive:
            for file in sorted(output.rglob("*")):
                if file.is_file():
                    archive.write(file, file.relative_to(output).as_posix())
        replacement.replace(args.out)
    print("Generated compatible multiplayer motions:", args.out)


if __name__ == "__main__":
    main()
