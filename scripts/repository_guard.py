"""Audit tracked source or export a clean snapshot, without copying Git history.

No game data is required. Export is deliberately limited to reviewed source
directories. It never deletes files or edits the source checkout/index.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
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
    "editor",
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
    ".gitmodules",
    ".clang-format",
    ".clang-format-ignore",
    ".clang-tidy",
    ".editorconfig",
    ".git-blame-ignore-revs",
    "pyproject.toml",
    "cliff.toml",
    "requirements-dev.txt",
    "README.md",
    "BUILDING.md",
    "THIRD_PARTY.md",
    "LICENSES.md",
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
    # The Order of the Flame campaign cover (7680 x 4800): the title art of the converted campaign, shipped by the builder
    # because an install folder holds no title picture. Tracked on the owner's decision; see THIRD_PARTY.md.
    "scripts/buildtool/kit/art/ootf-cover.png": "e1ac723f61550d1fa58e45b42df50cb112ff2cdbc86f3f0acbf46d8813c3cdbc",
}
TOKEN_PATTERNS = [
    re.compile(rb"-----BEGIN (?:RSA |OPENSSH |EC )?PRIVATE KEY-----"),
    re.compile(rb"\bgh[pousr]_[A-Za-z0-9]{30,}\b"),
    re.compile(rb"\bgithub_pat_[A-Za-z0-9_]{40,}\b"),
]


def tracked() -> list[str]:
    # Submodules (gitlinks, mode 160000) are separate repositories with their own audit.
    raw = subprocess.check_output(["git", "ls-files", "-s", "-z"], cwd=ROOT)
    out = []
    for rec in raw.split(b"\0"):
        if not rec:
            continue
        meta, _, name = rec.partition(b"\t")
        if meta.split()[0] != b"160000":
            out.append(name.decode("utf-8"))
    return out


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


def gitlinks(revision: str) -> dict[str, str]:
    """Submodule path -> commit recorded in a revision's tree."""
    rows = subprocess.check_output(["git", "ls-tree", "-r", "-z", revision], cwd=ROOT).decode().split("\0")
    links = {}
    for row in rows:
        if row.startswith("160000 commit "):
            meta, path = row.split("\t", 1)
            links[path] = meta.split()[2]
    return links


def unpublished_submodule_commits(revision: str, remote_revision: str) -> list[tuple[str, str]]:
    """Submodule commits this push starts pointing at that no remote-tracking ref of the submodule contains.

    Pushing such a pointer breaks every `submodules: recursive` checkout ("not our ref"), as run 42202148099 did.
    """
    before = {} if set(remote_revision) == {"0"} else gitlinks(remote_revision)
    missing = []
    for path, commit in gitlinks(revision).items():
        if before.get(path) == commit:
            continue
        submodule = ROOT / path
        if not (submodule / ".git").exists():
            missing.append((path, commit))
            continue
        result = subprocess.run(
            ["git", "-C", str(submodule), "for-each-ref", "--contains", commit, "--format=%(refname)", "refs/remotes/"],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0 or not result.stdout.strip():
            missing.append((path, commit))
    return missing


# Trees whose compiled sources decide whether the shipped game builds. The Setup and the builder kit are a git archive
# of a commit, so an edit that exists only in the working tree makes local builds pass and the release fail (v0.1.6).
BUILD_INPUT_PATHS = ("ps2recomp", "runtime", "cmake", "CMakeLists.txt", "CMakePresets.json", "configs", "third_party")


def uncommitted_build_inputs() -> list[str]:
    """Tracked files under BUILD_INPUT_PATHS that differ from HEAD (staged or not)."""
    out = subprocess.check_output(["git", "diff", "--name-only", "-z", "HEAD", "--", *BUILD_INPUT_PATHS], cwd=ROOT)
    return [name for name in out.decode().split("\0") if name]


def pre_push(remote: str | None = None) -> int:
    dirty = uncommitted_build_inputs()
    if dirty and os.environ.get("DRAKAN_ALLOW_DIRTY_PUSH") != "1":
        print(
            "Push rejected: tracked build inputs have uncommitted edits, so local builds would pass on code that\n"
            "is not in the commit you are pushing (the release Setup is built from the commit alone):",
            file=sys.stderr,
        )
        for name in dirty[:20]:
            print(f"  {name}", file=sys.stderr)
        print("Commit or stash them (DRAKAN_ALLOW_DIRTY_PUSH=1 overrides).", file=sys.stderr)
        return 1
    # Only the destination's own refs count as already published: an object another remote holds is new here.
    published = [f"--remotes={remote}"] if remote else []
    failures = []
    unpublished = []
    for line in sys.stdin:
        fields = line.split()
        if len(fields) != 4 or set(fields[1]) == {"0"}:
            continue
        ref, revision, _, remote_revision = fields
        unpublished += [
            (ref, path, commit) for path, commit in unpublished_submodule_commits(revision, remote_revision)
        ]
        # Audit objects the destination lacks; its existing history is NOT certified and needs a full history audit.
        known = ["--not", *published] + ([] if set(remote_revision) == {"0"} else [remote_revision])
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
    if unpublished:
        print(
            "Push rejected: it points at submodule commits that are not on the submodule's remote yet.", file=sys.stderr
        )
        for ref, path, commit in unpublished:
            print(f"  {ref}: {path} @ {commit[:12]}", file=sys.stderr)
        print(
            "Push the submodule first (git -C <path> push), or fetch it if someone else already did.", file=sys.stderr
        )
        return 1
    print("Push source/history audit passed.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--export", type=Path, help="new empty destination for approved tracked files only")
    parser.add_argument("--report", type=Path, help="optional JSON audit report")
    parser.add_argument(
        "--pre-push",
        nargs="?",
        const="",
        metavar="REMOTE",
        help="audit objects the named remote lacks, for refs supplied by Git (the hook passes the remote name)",
    )
    args = parser.parse_args()
    if args.pre_push is not None:
        return pre_push(args.pre_push or None)
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
