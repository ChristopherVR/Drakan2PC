"""Bootstrap public Linux tools, then use the canonical ISO build recipe."""

import argparse
import ctypes.util
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]


def run(args, **kwargs):
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def download(url, path, digest):
    if not path.is_file():
        print(f"Downloading public tool: {url}", flush=True)
        urllib.request.urlretrieve(url, path)
    if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
        raise RuntimeError(f"Tool archive checksum mismatch: {path}")


def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with tarfile.open(archive) as source:
        for member in source:
            target = (root / member.name).resolve()
            if not target.is_relative_to(root) or member.isdev() or member.isfifo():
                raise ValueError("Unsafe public tool archive member")
            if member.issym() or member.islnk():
                link = (target.parent if member.issym() else root) / member.linkname
                if not link.resolve().is_relative_to(root):
                    raise ValueError("Unsafe public tool archive link")
            source.extract(member, root)


def dependencies(web):
    commands = ["git", "clang", "cmake", "ninja", "make", "pkg-config"]
    modules = (
        []
        if web
        else [
            "alsa",
            "libavcodec",
            "libavformat",
            "libavutil",
            "libswscale",
            "libswresample",
            "x11",
            "xrandr",
            "xi",
            "xcursor",
            "xinerama",
            "gl",
        ]
    )
    missing = any(not shutil.which(command) for command in commands)
    if not missing and modules:
        missing = subprocess.run(["pkg-config", "--exists", *modules]).returncode != 0
    if not missing:
        # A pre-existing compiler does not guarantee PowerShell's runtime
        # libraries, or the ALSA plugin used by WSLg, are installed.
        missing = any(not ctypes.util.find_library(name) for name in ("icuuc", "ssl", "z"))
    if not missing and not web and os.environ.get("PULSE_SERVER") and shutil.which("dpkg-query"):
        probe = subprocess.run(
            ["dpkg-query", "-W", "-f=${db:Status-Status}", "libasound2-plugins:amd64"],
            capture_output=True,
            text=True,
        )
        missing = probe.returncode != 0 or probe.stdout != "installed"
    if not missing:
        return
    if not shutil.which("apt-get"):
        raise RuntimeError("Automatic dependency setup supports Debian/Ubuntu. See BUILDING.md for other systems.")
    prefix = [] if os.geteuid() == 0 else ["sudo"]
    packages = [
        "build-essential",
        "clang",
        "cmake",
        "ninja-build",
        "git",
        "pkg-config",
        "ca-certificates",
        "libicu-dev",
        "libssl-dev",
        "zlib1g-dev",
    ]
    if not web:
        packages += [
            "libasound2-dev",
            "libasound2-plugins",
            "libavcodec-dev",
            "libavformat-dev",
            "libavutil-dev",
            "libswscale-dev",
            "libswresample-dev",
            "libx11-dev",
            "libxrandr-dev",
            "libxi-dev",
            "libxcursor-dev",
            "libxinerama-dev",
            "libgl1-mesa-dev",
        ]
    print("Installing public build dependencies through the system package manager.", flush=True)
    run([*prefix, "apt-get", "update"])
    run([*prefix, "apt-get", "install", "-y", *packages])


def tools():
    cache = ROOT / "build/tools"
    cache.mkdir(parents=True, exist_ok=True)
    lock = json.loads((HERE / "web-toolchain.lock.json").read_text())
    python = sys.executable
    if sys.version_info < (3, 11):  # noqa: UP036 - bootstrap also runs on Ubuntu 22.04 Python 3.10
        spec = lock["linux_python"]
        uv = cache / "uv/uv-x86_64-unknown-linux-gnu/uv"
        if not uv.is_file():
            archive = cache / "uv.tar.gz"
            download(
                f"https://github.com/astral-sh/uv/releases/download/{spec['uv_version']}/uv-x86_64-unknown-linux-gnu.tar.gz",
                archive,
                spec["uv_sha256"],
            )
            extract(archive, cache / "uv")
        run([uv, "python", "install", spec["version"], "--install-dir", cache / "python"])
        python = next((cache / "python").glob(f"cpython-{spec['version']}-linux-x86_64-gnu/bin/python3.12"))
    pwsh = shutil.which("pwsh")
    if not pwsh:
        spec = lock["powershell"]
        folder = cache / f"powershell-{spec['version']}"
        pwsh = folder / "pwsh"
        if not pwsh.is_file():
            archive = cache / f"powershell-{spec['version']}.tar.gz"
            download(
                f"https://github.com/PowerShell/PowerShell/releases/download/v{spec['version']}/powershell-{spec['version']}-linux-x64.tar.gz",
                archive,
                spec["sha256"],
            )
            extract(archive, folder)
            pwsh.chmod(0o755)
    return pwsh, python


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("iso", type=Path)
    parser.add_argument("--web", action="store_true", help="Build the private browser player")
    parser.add_argument("--work-dir", type=Path, default=ROOT / "build/final-game")
    parser.add_argument("--texture-dir", type=Path, help="Optional shared HD texture pack")
    parser.add_argument("--jobs", type=int, choices=range(1, 17), default=2)
    args = parser.parse_args()
    if sys.platform != "linux" or platform.machine() not in ("x86_64", "AMD64"):
        parser.error("This bootstrap supports x86-64 Linux, including WSL.")
    iso = args.iso.resolve(strict=True)
    if not iso.is_file():
        parser.error("The ISO must be a file")
    dependencies(args.web)
    pwsh, python = tools()
    work = args.work_dir.resolve()
    command = [
        pwsh,
        "-NoProfile",
        "-File",
        HERE / "Build-LocalGame.ps1",
        "-IsoPath",
        iso,
        "-WorkDir",
        work,
        "-TargetPlatform",
        "web" if args.web else "linux",
        "-Jobs",
        args.jobs,
        "-Python",
        python,
    ]
    if (work / "local-input.json").is_file():
        command += ["-Incremental"]
    if args.texture_dir:
        command += ["-TextureDirectory", args.texture_dir.resolve(strict=True)]
    run(command, cwd=ROOT)


if __name__ == "__main__":
    main()
