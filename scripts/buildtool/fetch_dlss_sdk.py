#!/usr/bin/env python3
"""Download the parts of NVIDIA's public DLSS SDK that the build needs, after the user accepts NVIDIA's licence.

The SDK is never redistributed by this project: each player downloads it from NVIDIA's own repository
(github.com/NVIDIA/DLSS, pinned tag and SHA-256 in dlss.lock.json) onto their PC. Without
--accept-nvidia-license the script only prints the licence location and exits.

  fetch_dlss_sdk.py --accept-nvidia-license      # -> tools/dlss/dlss_sdk (Build-LocalGame.ps1 stages its DLL)
"""

from __future__ import annotations

import argparse
import hashlib
import json
import shutil
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
LOCK = HERE / "dlss.lock.json"
RUNTIME_DLL = "lib/Windows_x86_64/rel/nvngx_dlss.dll"
ACCEPTANCE = ".nvidia-license-accepted"


def load_lock() -> dict:
    return json.loads(LOCK.read_text(encoding="utf-8"))


def licence_url(lock: dict) -> str:
    return f"https://github.com/{lock['repository']}/blob/{lock['tag']}/LICENSE.txt"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(url: str, target: Path) -> None:
    for attempt in range(3):
        try:
            with urllib.request.urlopen(url, timeout=120) as response, target.open("wb") as output:
                shutil.copyfileobj(response, output)
            return
        except OSError:
            if attempt == 2:
                raise
            time.sleep(2 * (attempt + 1))


def fetch(lock: dict, destination: Path, runtime_destination: Path | None) -> None:
    base = f"https://raw.githubusercontent.com/{lock['repository']}/{lock['tag']}/"
    destination.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="drakan-dlss-") as scratch:
        for relative, expected in sorted(lock["files"].items()):
            target = destination / relative
            if target.is_file() and sha256(target) == expected:
                continue
            print(f"  {relative}", flush=True)
            staged = Path(scratch) / Path(relative).name
            download(base + relative, staged)
            if sha256(staged) != expected:
                raise RuntimeError(f"{relative} from NVIDIA does not match the SHA-256 pinned in {LOCK.name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.move(str(staged), target)
    (destination / ACCEPTANCE).write_text(
        f"NVIDIA RTX SDKs licence accepted on {time.strftime('%Y-%m-%d')} for {lock['repository']} {lock['tag']}\n"
        f"{licence_url(lock)}\n",
        encoding="utf-8",
    )
    if runtime_destination:
        runtime_destination.mkdir(parents=True, exist_ok=True)
        shutil.copy2(destination / RUNTIME_DLL, runtime_destination / "nvngx_dlss.dll")


def update_lock(lock: dict) -> None:
    """Maintainer helper: download the listed files at lock['tag'] and record their hashes."""
    base = f"https://raw.githubusercontent.com/{lock['repository']}/{lock['tag']}/"
    with tempfile.TemporaryDirectory(prefix="drakan-dlss-") as scratch:
        for relative in sorted(lock["files"]):
            staged = Path(scratch) / "file"
            download(base + relative, staged)
            lock["files"][relative] = sha256(staged)
    LOCK.write_text(json.dumps(lock, indent=2) + "\n", encoding="utf-8", newline="\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--accept-nvidia-license", action="store_true", help="accept the NVIDIA RTX SDKs licence")
    parser.add_argument("--dest", type=Path, default=REPO / "tools" / "dlss" / "dlss_sdk")
    parser.add_argument("--runtime-dest", type=Path, help="also copy nvngx_dlss.dll into this folder")
    parser.add_argument("--update-lock", action="store_true", help=argparse.SUPPRESS)
    args = parser.parse_args()
    lock = load_lock()
    if args.update_lock:
        update_lock(lock)
        return 0
    if not args.accept_nvidia_license:
        print(
            "NVIDIA DLSS is optional and only works on NVIDIA RTX GPUs. The SDK is downloaded from NVIDIA under the\n"
            f"NVIDIA RTX SDKs licence: {licence_url(lock)}\n"
            "Run again with --accept-nvidia-license to accept it and download the SDK."
        )
        return 2
    print(f"Downloading the NVIDIA DLSS SDK {lock['tag']} from github.com/{lock['repository']} ...", flush=True)
    try:
        fetch(lock, args.dest.resolve(), args.runtime_dest.resolve() if args.runtime_dest else None)
    except (OSError, RuntimeError) as error:
        print(f"DLSS SDK download failed: {error}", file=sys.stderr)
        return 1
    print(f"DLSS SDK ready in {args.dest}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
