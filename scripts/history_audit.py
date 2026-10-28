#!/usr/bin/env python3
"""Audit every Git blob reachable from local refs with the repository guard's rules.

Fetch all branches and tags first; the scan covers everything `git rev-list --all` reaches.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from repository_guard import ROOT, reason

MAX_BLOB_BYTES = 2 * 1024 * 1024
BATCH_SIZE = 32


def git(*args: str, input_bytes: bytes | None = None) -> bytes:
    return subprocess.check_output(["git", *args], cwd=ROOT, input=input_bytes)


def enumerate_blobs() -> list[tuple[str, str, int]]:
    """Return (object_id, last-seen path, uncompressed size) for reachable blobs."""
    named = []
    for raw in git("rev-list", "--objects", "--all").splitlines():
        oid, _, raw_path = raw.partition(b" ")
        named.append((oid.decode("ascii"), raw_path.decode("utf-8", "surrogateescape")))
    result = []
    for start in range(0, len(named), 2048):
        group = named[start : start + 2048]
        info = git(
            "cat-file",
            "--batch-check=%(objecttype) %(objectsize)",
            input_bytes="".join(f"{oid}\n" for oid, _ in group).encode("ascii"),
        ).splitlines()
        if len(info) != len(group):
            raise ValueError("Git returned fewer object descriptions than requested")
        for (oid, path), item in zip(group, info, strict=True):
            parts = item.split()
            if len(parts) == 2 and parts[0] == b"blob":
                result.append((oid, path, int(parts[1])))
    return result


def audit() -> dict:
    blobs = enumerate_blobs()
    failures: list[dict[str, str | int]] = []
    safe = []
    for oid, path, size in blobs:
        # No filename can make a >2 MiB blob safe for this source-only policy.
        why = "historical blob exceeds 2 MiB" if size > MAX_BLOB_BYTES else reason(path)
        if why:
            failures.append({"path": path or "(unresolved historical path)", "oid": oid, "bytes": size, "reason": why})
        else:
            safe.append((oid, path))
    # Inspect all remaining content, not merely its extension: executable
    # headers, private keys and disassembly patches may be disguised as text.
    for start in range(0, len(safe), BATCH_SIZE):
        group = safe[start : start + BATCH_SIZE]
        raw = git("cat-file", "--batch", input_bytes="".join(f"{oid}\n" for oid, _ in group).encode("ascii"))
        offset = 0
        for oid, path in group:
            eol = raw.index(b"\n", offset)
            header = raw[offset:eol].split()
            if len(header) != 3 or header[0].decode("ascii") != oid or header[1] != b"blob":
                raise ValueError("Unexpected Git blob batch response")
            size = int(header[2])
            data = raw[eol + 1 : eol + 1 + size]
            if len(data) != size or raw[eol + 1 + size : eol + 2 + size] != b"\n":
                raise ValueError("Truncated Git blob batch response")
            offset = eol + size + 2
            why = reason(path, data)
            if why:
                failures.append({"path": path, "oid": oid, "bytes": size, "reason": why})
    return {
        "scope": "local reachable git refs (--all); fetch all remote branches, tags and PR refs first",
        "blobs_checked": len(blobs),
        "failures": failures,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path, help="write JSON findings without exposing blob contents")
    args = parser.parse_args()
    try:
        result = audit()
    except (OSError, subprocess.CalledProcessError, ValueError) as exc:
        print(f"History audit could not complete: {exc}", file=sys.stderr)
        return 2
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"History audit: {result['blobs_checked']} reachable blobs, {len(result['failures'])} violations")
    for item in result["failures"][:30]:
        print(f"  {item['path']}: {item['reason']} ({item['bytes']} bytes, {item['oid'][:12]})")
    if len(result["failures"]) > 30:
        print(f"  ...and {len(result['failures']) - 30} more; use --report for the full list")
    if result["failures"]:
        return 1
    print("History audit passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
