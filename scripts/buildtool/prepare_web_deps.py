"""Prepare pinned public browser tools/FFmpeg; never read or distribute game data."""

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent


def run(args, **kwargs):
    print("+", subprocess.list2cmdline([str(arg) for arg in args]), flush=True)
    subprocess.run([str(arg) for arg in args], check=True, **kwargs)


def unpack(archive, folder):
    """Extract regular sources only; reject archives escaping the dependency root."""
    folder = folder.resolve()
    with tarfile.open(archive) as source:
        for member in source:
            target = (folder / member.name).resolve()
            if not target.is_relative_to(folder):
                raise ValueError("Archive member escapes the dependency folder")
            if member.isdir():
                target.mkdir(parents=True, exist_ok=True)
            elif member.isfile():
                target.parent.mkdir(parents=True, exist_ok=True)
                with source.extractfile(member) as data, target.open("wb") as output:
                    shutil.copyfileobj(data, output)
                target.chmod(member.mode & 0o777)
            else:
                raise ValueError("Unexpected link/device in the dependency archive")


def ensure_sdk(repo):
    if os.name == "nt":
        raise RuntimeError("Prepare browser dependencies on Linux (Windows can use WSL)")
    lock = json.loads((HERE / "web-toolchain.lock.json").read_text())
    sdk = repo / "build/tools/emsdk"
    spec = lock["emsdk"]
    if not (sdk / ".git").is_dir():
        sdk.mkdir(parents=True, exist_ok=True)
        run(["git", "init", sdk])
        run(["git", "-C", sdk, "fetch", "--depth=1", "https://github.com/emscripten-core/emsdk.git", spec["commit"]])
        run(["git", "-C", sdk, "checkout", "--detach", "FETCH_HEAD"])
    revision = subprocess.check_output(["git", "-C", sdk, "rev-parse", "HEAD"], text=True).strip()
    if revision != spec["commit"]:
        raise RuntimeError("The existing emsdk cache differs from web-toolchain.lock.json")
    emcc = sdk / "upstream/emscripten/emcc"
    if not emcc.is_file():
        run([sys.executable, sdk / "emsdk.py", "install", spec["version"]])
        run([sys.executable, sdk / "emsdk.py", "activate", spec["version"]])
    sdk_alias = Path(tempfile.gettempdir()) / ("drakan-emsdk-" + hashlib.sha256(str(sdk).encode()).hexdigest()[:16])
    if sdk_alias.is_symlink() and sdk_alias.resolve() != sdk.resolve():
        raise RuntimeError("Unexpected Emscripten tool alias")
    if not sdk_alias.exists():
        sdk_alias.symlink_to(sdk, target_is_directory=True)
    emcc = sdk_alias / "upstream/emscripten/emcc"
    env = dict(os.environ)
    env["PATH"] = str(emcc.parent) + os.pathsep + env.get("PATH", "")
    return sdk, emcc, env


def prepare(repo, work, jobs):
    _, emcc, env = ensure_sdk(repo)
    lock = json.loads((HERE / "web-toolchain.lock.json").read_text())
    deps = work / "web-deps"
    deps.mkdir(parents=True, exist_ok=True)
    ffmpeg = lock["ffmpeg"]
    archive = deps / f"ffmpeg-{ffmpeg['version']}.tar.xz"
    if not archive.is_file():
        print("Downloading public FFmpeg sources", flush=True)
        urllib.request.urlretrieve(ffmpeg["url"], archive)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != ffmpeg["sha256"]:
        raise RuntimeError("FFmpeg source archive SHA-256 mismatch")
    sources = deps / "sources"
    source = sources / f"ffmpeg-{ffmpeg['version']}"
    if not (source / "configure").is_file():
        unpack(archive, sources)
    prefix = deps / "ffmpeg-install"
    expected_build = {"emsdk": lock["emsdk"], "ffmpeg": ffmpeg}
    marker = prefix / "drakan-complete.json"
    try:
        completed_build = json.loads(marker.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        completed_build = None
    if completed_build != expected_build:
        # GNU make/configure need a whitespace-free spelling of the same private
        # dependency directory. Sources/outputs still live inside WorkDir.
        key = hashlib.sha256(str(deps).encode()).hexdigest()[:16]
        alias = Path(tempfile.gettempdir()) / f"drakan-web-deps-{key}"
        if alias.is_symlink() and alias.resolve() != deps.resolve():
            raise RuntimeError("Unexpected browser dependency alias")
        if not alias.exists():
            alias.symlink_to(deps, target_is_directory=True)
        build = alias / "ffmpeg-build"
        build.mkdir(exist_ok=True)
        env["PWD"] = str(build)
        run(
            [
                alias / "sources" / source.name / "configure",
                f"--prefix={alias / 'ffmpeg-install'}",
                "--cc=emcc",
                "--cxx=em++",
                "--ar=emar",
                "--ranlib=emranlib",
                "--nm=emnm",
                "--enable-cross-compile",
                "--target-os=none",
                "--arch=wasm32",
                "--disable-autodetect",
                "--disable-everything",
                "--disable-programs",
                "--disable-doc",
                "--disable-network",
                "--disable-pthreads",
                "--disable-w32threads",
                "--disable-os2threads",
                "--disable-x86asm",
                "--disable-inline-asm",
                "--disable-stripping",
                "--enable-decoder=mpeg2video",
                "--enable-parser=mpegvideo",
                "--enable-swscale",
                "--extra-cflags=-O3 -pthread -msimd128",
                "--extra-ldflags=-pthread",
            ],
            cwd=build,
            env=env,
        )
        run(["make", f"-j{jobs}"], cwd=build, env=env)
        run(["make", "install"], cwd=build, env=env)
        marker.write_text(json.dumps(expected_build) + "\n", encoding="utf-8")
    result = {"toolchain": str(emcc.parent / "cmake/Modules/Platform/Emscripten.cmake"), "ffmpeg": str(prefix)}
    (deps / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo-root", type=Path, default=HERE.parents[1])
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=2)
    args = parser.parse_args()
    prepare(args.repo_root.resolve(), args.work_dir.resolve(), args.jobs)
