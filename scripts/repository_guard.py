"""Audit tracked source or export a clean snapshot, without copying Git history.

No game data is required. Export is deliberately limited to reviewed source
directories. It never deletes files or edits the source checkout/index.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[1]
SOURCE_ROOTS = {
    ".github",
    ".githooks",
    "analysis",
    "cmake",
    "configs",
    "docs",
    "licenses",
    "ps2recomp",
    "runtime",
    "scripts",
    "third_party",
}
ROOT_FILES = {
    "CMakeLists.txt",
    "CMakePresets.json",
    ".gitignore",
    ".gitattributes",
    ".clang-format",
    ".clang-format-ignore",
    ".clang-tidy",
    ".editorconfig",
    ".git-blame-ignore-revs",
    "pyproject.toml",
    "requirements-dev.txt",
    "README.md",
    "BUILDING.md",
    "THIRD_PARTY.md",
    "CLAUDE.md",
    "AGENTS.md",
    "LICENSE",
}
BINARY_SUFFIXES = {
    ".iso",
    ".img",
    ".chd",
    ".cso",
    ".elf",
    ".irx",
    ".bin",
    ".exe",
    ".dll",
    ".obj",
    ".o",
    ".a",
    ".lib",
    ".pdb",
    ".ram",
    ".rsg",
    ".ps2",
    ".mcd",
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
    ".gscap",
    ".zip",
    ".7z",
    ".tar",
    ".gz",
    ".png",
    ".jpg",
    ".jpeg",
    ".bmp",
    ".tga",
    ".dds",
    ".webp",
    ".ico",
    ".pss",
    ".vag",
    ".adp",
    ".mdf",
    ".mds",
    ".nrg",
    ".cue",
}
# Reviewed binaries that may be tracked; pinned by SHA-256 so a swapped file is rejected.
ALLOWED_BINARIES = {
    "ps2recomp/ps2xRuntime/src/res/drakan.ico": "92ae70a057c098687dff194a7bd972b793012d178510e20fab22c8943d077946",
}
TOKEN_PATTERNS = [
    re.compile(rb"-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----"),
    re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b"),
    re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b"),
]


def tracked() -> list[str]:
    raw = subprocess.check_output(["git", "ls-files", "-z"], cwd=ROOT)
    return [p.decode("utf-8") for p in raw.split(b"\0") if p]


def reason(name: str, content: bytes | None = None) -> str | None:
    p = PurePosixPath(name)
    parts = p.parts
    if not parts or p.is_absolute() or ".." in parts:
        return "unsafe path"
    if name in ALLOWED_BINARIES:
        if content is not None and hashlib.sha256(content).hexdigest() != ALLOWED_BINARIES[name]:
            return "allowed binary differs from its pinned SHA-256"
        return None
    if len(parts) == 1 and name not in ROOT_FILES:
        return "not a reviewed root source file"
    if len(parts) > 1 and parts[0] not in SOURCE_ROOTS:
        return "local dependency, generated output, save or game directory"
    if any(s.lower() in {"mc0", "mc1", "node_modules", "__pycache__", ".git"} for s in parts):
        return "local state directory"
    if p.suffix.lower() in BINARY_SUFFIXES:
        return "game data, transcript, archive or compiled output"
    if re.match(r"^(?:SCES|SCUS|SLES|SLUS)[_-]\d", p.name, re.I):
        return "disc executable"
    if name.startswith(("analysis/sound/", "analysis/input/")) and p.suffix in {".c", ".cpp", ".asm"}:
        return "generated game decompilation"
    if name.startswith(("analysis/vu/", "analysis/iop/embedded", "analysis/iop/ioprp_modules/")):
        return "generated game or SDK dump"
    if name in {"analysis/strings_vaddr.csv", "analysis/sdk_strings.csv"}:
        return "raw strings extracted from the game"
    if name == "analysis/iop/iso_listing.txt":
        return "file listing of the game disc"
    if name.startswith("docs/proposals/subtitles/") and p.name != "README.txt":
        return "local soundtrack transcript or timestamps"
    if p.name == "ps2_vu1_image.inc":
        return "game VU1 microcode image (generated locally by scripts/prepare_local_runtime.py)"
    if content is not None:
        if content.startswith((b"MZ", b"\x7fELF")):
            return "executable content regardless of filename"
        if len(content) > 2 * 1024 * 1024:
            return "unexpected source file larger than 2 MiB"
        if any(pattern.search(content) for pattern in TOKEN_PATTERNS):
            return "credential/private key signature (value omitted)"
        if p.suffix == ".patch" and (
            b"\nGIT binary patch\n" in content or re.search(rb"(?m)^diff --git a/[^\n]*ps2_vu1_image\.inc ", content)
        ):
            return "patch contains a binary or game instruction image"
    return None


def pre_push() -> int:
    failures = []
    for line in sys.stdin:
        fields = line.split()
        if len(fields) != 4 or set(fields[1]) == {"0"}:
            continue
        ref, revision, _, remote_revision = fields
        # Audit new objects only; remote history is NOT certified and requires a separate full history audit.
        known = ["--not", "--remotes"] + ([] if set(remote_revision) == {"0"} else [remote_revision])
        rows = (
            subprocess.check_output(["git", "rev-list", "--objects", revision, *known], cwd=ROOT).decode().splitlines()
        )
        entries = [row.split(" ", 1) for row in rows if " " in row]
        ids = b"".join(oid.encode() + b"\n" for oid, _ in entries)
        kinds = (
            subprocess.check_output(["git", "cat-file", "--batch-check=%(objecttype)"], cwd=ROOT, input=ids)
            .decode()
            .splitlines()
        )
        blobs = [(oid, name) for (oid, name), kind in zip(entries, kinds) if kind == "blob"]
        payload = subprocess.check_output(
            ["git", "cat-file", "--batch"], cwd=ROOT, input=b"".join(oid.encode() + b"\n" for oid, _ in blobs)
        )
        offset = 0
        for _, name in blobs:
            end = payload.index(b"\n", offset)
            size = int(payload[offset:end].split()[-1])
            data = payload[end + 1 : end + 1 + size]
            offset = end + 2 + size
            why = reason(name, data)
            if why:
                failures.append({"ref": ref, "path": name, "reason": why})
    if failures:
        print("Push rejected: reachable history includes local/generated files.", file=sys.stderr)
        for entry in failures[:20]:
            print(f"  {entry['ref']}: {entry['path']}: {entry['reason']}", file=sys.stderr)
        return 1
    print("Push source/history audit passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, help="new empty destination for approved tracked files only")
    parser.add_argument("--report", type=Path, help="optional JSON audit report")
    parser.add_argument(
        "--pre-push", action="store_true", help="audit every object reachable from refs supplied by Git"
    )
    args = parser.parse_args()
    if args.pre_push:
        return pre_push()
    approved, blocked = [], []
    for name in tracked():
        path = ROOT / name
        why = reason(name)
        if why is None:
            if path.is_symlink():
                why = "symlink requires explicit review"
            elif not path.is_file():
                why = "tracked file unavailable"
            else:
                why = reason(name, path.read_bytes())
        if why:
            blocked.append({"path": name, "reason": why})
        else:
            approved.append(name)
    report = {"approved_count": len(approved), "blocked": blocked, "approved": approved}
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    if args.export:
        if blocked:
            print("Refusing source export: tracked files failed audit.", file=sys.stderr)
            for entry in blocked[:20]:
                print(f"  {entry['path']}: {entry['reason']}", file=sys.stderr)
            return 1
        # Avoid publishing a source snapshot that includes unstaged or staged edits not in HEAD.
        dirty = subprocess.run(
            ["git", "status", "--porcelain", "--untracked-files=no"],
            cwd=ROOT,
            capture_output=True,
            check=True,
        )
        if dirty.stdout:
            print("Refusing source export: tracked working tree differs from HEAD.", file=sys.stderr)
            return 1
        destination = args.export.resolve()
        if destination.exists():
            parser.error("export destination must not already exist")
        destination.mkdir(parents=True)
        for name in approved:
            target = destination / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(ROOT / name, target)
        print(f"Exported {len(approved)} audited tracked files. No Git history copied.")
        return 0
    print(f"Repository source audit: {len(approved)} approved; {len(blocked)} prohibited tracked files.")
    for entry in blocked:
        print(f"  {entry['path']}: {entry['reason']}")
    return 1 if blocked else 0


if __name__ == "__main__":
    sys.exit(main())
