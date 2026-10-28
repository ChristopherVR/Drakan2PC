#!/usr/bin/env python3
"""Reject game/SDK assets in compiler-only GitHub Actions artifacts."""

from __future__ import annotations

import argparse
import hashlib
import io
import json
import sys
import zipfile
from pathlib import Path, PurePosixPath

DISALLOWED = {
    ".iso",
    ".elf",
    ".irx",
    ".bin",
    ".dll",
    ".exe",
    ".img",
    ".chd",
    ".rsg",
    ".rrc",
    ".vdb",
    ".xdb",
    ".sdb",
    ".txd",
    ".wav",
    ".mp3",
    ".ogg",
    ".mp4",
    ".srt",
    ".vtt",
    ".png",
    ".jpg",
    ".jpeg",
    ".ico",
    ".pss",
    ".zip",
    ".7z",
    ".tar",
    ".gz",
    ".cso",
}
SOURCE_ZIP = "ps2-recomp-corresponding-source.zip"
REQUIRED_TOOL = {"NOTICES.txt", "LICENSE-PS2Recomp.txt", "manifest.json", SOURCE_ZIP}


def validate_zip(data: bytes, *, source: bool) -> dict[str, bytes]:
    result: dict[str, bytes] = {}
    with zipfile.ZipFile(io.BytesIO(data)) as zf:
        names = zf.namelist()
        if len(names) != len(set(names)):
            raise ValueError("duplicate ZIP entry")
        for info in zf.infolist():
            name = info.filename
            path = PurePosixPath(name)
            if (
                info.is_dir()
                or path.is_absolute()
                or ".." in path.parts
                or "\\" in name
                or info.file_size > (128 if name in {"ps2_recomp", "ps2_recomp.exe", SOURCE_ZIP} else 8) * 1024 * 1024
            ):
                raise ValueError(f"unexpected ZIP entry: {name}")
            if path.suffix.lower() in DISALLOWED:
                if not (not source and name in {SOURCE_ZIP, "ps2_recomp.exe"}):
                    raise ValueError(f"unapproved asset or nested archive: {name}")
            if not source and (path.name in {"ps2_recomp", "ps2_recomp.exe"}):
                if name != path.name:
                    raise ValueError("compiler executable must be at package root")
            blob = zf.read(name)
            if name not in {"ps2_recomp", "ps2_recomp.exe", SOURCE_ZIP}:
                if blob.startswith((b"MZ", b"\x7fELF")):
                    raise ValueError(f"embedded executable content: {name}")
            if b"kVu1Image[" in blob:
                raise ValueError(f"embedded proprietary game microcode: {name}")
            result[name] = blob
    if source:
        if "PS2Recomp/ps2recomp/LICENSE" not in result:
            raise ValueError("corresponding source archive lacks PS2Recomp GPL license")
        if any(name.startswith("PS2Recomp/ps2xRuntime/src/res/") for name in result):
            raise ValueError("game runtime assets included in compiler source")
    else:
        if not REQUIRED_TOOL.issubset(result):
            raise ValueError(f"missing compiler notices/source: {sorted(REQUIRED_TOOL - result.keys())}")
        executables = [name for name in result if name in {"ps2_recomp", "ps2_recomp.exe"}]
        if len(executables) != 1:
            raise ValueError("expected exactly one compiler executable")
        manifest = json.loads(result["manifest.json"])
        digest = hashlib.sha256(result[executables[0]]).hexdigest()
        if manifest["compiler_sha256"] != digest:
            raise ValueError("compiler hash mismatch in archive manifest")
        validate_zip(result[SOURCE_ZIP], source=True)
    return result


def check(directory) -> None:
    source_path = directory / SOURCE_ZIP
    sums_path = directory / "SHA256SUMS.txt"
    if not source_path.is_file() or not sums_path.is_file():
        raise ValueError("compiler artifact source archive or SHA256SUMS is missing")
    validate_zip(source_path.read_bytes(), source=True)
    expected = {}
    for line in sums_path.read_text(encoding="ascii").splitlines():
        digest, separator, name = line.partition("  ")
        if not separator or len(digest) != 64 or name in expected:
            raise ValueError("invalid SHA256SUMS line")
        expected[name] = digest
    zipped = sorted(directory.glob("*.zip"))
    if {p.name for p in zipped} != set(expected):
        raise ValueError("SHA256SUMS filenames differ from packaged archives")
    tool_zips = [p for p in zipped if p.name != SOURCE_ZIP]
    if len(tool_zips) != 1:
        raise ValueError("expected one compiler-only tool archive")
    for path in zipped:
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != expected[path.name]:
            raise ValueError(f"SHA256SUMS mismatch: {path.name}")
        if path.name != SOURCE_ZIP:
            contents = validate_zip(data, source=False)
            if contents[SOURCE_ZIP] != source_path.read_bytes():
                raise ValueError("tool archive corresponding source differs from standalone source ZIP")
    print(f"Compiler-only artifact audit passed: {tool_zips[0].name}, {SOURCE_ZIP}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", required=True, type=Path)
    args = parser.parse_args()
    try:
        check(args.directory)
    except (ValueError, OSError, zipfile.BadZipFile, KeyError, json.JSONDecodeError) as exc:
        print(f"Compiler package rejected: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
