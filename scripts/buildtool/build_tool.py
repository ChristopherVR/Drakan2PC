#!/usr/bin/env python3
"""Build only the translator (ps2_recomp) from the repository's root CMakeLists.txt (DRAKAN_BUILD_RECOMPILER).

No ISO, runtime, NVIDIA SDK or game generation. Third-party dependencies are
fetched at the commits pinned in toolchain.lock.json.
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1]
SOURCE = ROOT  # single CMake root; the compiler itself is ps2recomp/ps2xRecomp
LOCK = HERE / "toolchain.lock.json"


def run(args, cwd=None):
    print("+ " + subprocess.list2cmdline([str(a) for a in args]), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def output(args, cwd=None):
    return subprocess.check_output([str(a) for a in args], cwd=cwd, text=True).strip()


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def fetch(spec, target):
    if not target.exists():
        run(["git", "init", target])
        run(["git", "-C", target, "fetch", "--depth=1", spec["url"], spec["commit"]])
        run(["git", "-C", target, "checkout", "--detach", "FETCH_HEAD"])
    if output(["git", "-C", target, "rev-parse", "HEAD"]) != spec["commit"]:
        raise RuntimeError(f"Unexpected checkout at {target}; choose a fresh --work-dir")


def find_cmake():
    found = shutil.which("cmake")
    if found:
        return found
    if os.name == "nt":
        vswhere = (
            Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)"))
            / "Microsoft Visual Studio/Installer/vswhere.exe"
        )
        if vswhere.exists():
            install = output(
                [
                    vswhere,
                    "-latest",
                    "-products",
                    "*",
                    "-requires",
                    "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                    "-property",
                    "installationPath",
                ]
            )
            candidate = Path(install) / "Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
            if candidate.is_file():
                return str(candidate)
    raise RuntimeError("CMake 3.21+ is required (PATH or Visual Studio C++ CMake tools)")


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work-dir", type=Path, default=ROOT / "build/buildtool")
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--prepare-only", action="store_true", help="only fetch the pinned dependencies")
    args = ap.parse_args()
    if args.jobs < 1 or args.jobs > 16:
        ap.error("--jobs must be 1..16")
    if platform.machine().lower() not in ("amd64", "x86_64"):
        ap.error("This recipe currently targets x86-64 hosts only")
    work = args.work_dir.resolve()
    lock = json.loads(LOCK.read_text(encoding="utf-8"))
    source = SOURCE
    if not (source / "CMakeLists.txt").is_file():
        raise RuntimeError(f"Missing in-repo compiler source: {source}")
    work.mkdir(parents=True, exist_ok=True)
    marker = work / "recipe.json"
    marker.write_text(json.dumps({"lock": lock}, indent=2) + "\n", encoding="utf-8")
    for name, spec in lock["dependencies"].items():
        fetch(spec, work / "deps" / name)
    if args.prepare_only:
        return
    cmake = find_cmake()
    # Same configuration as the `recompiler` (Windows) / `linux-recompiler` presets in CMakePresets.json.
    config = [
        cmake,
        "-S",
        source,
        "--preset",
        "recompiler" if os.name == "nt" else "linux-recompiler",
        "-B",
        work / "cmake",
        "-DCMAKE_BUILD_TYPE=Release",
    ]
    config += [
        f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={(work / 'deps' / name).as_posix()}" for name in lock["dependencies"]
    ]
    run(config)
    run([cmake, "--build", work / "cmake", "--config", "Release", "--target", "ps2_recomp", "--parallel", args.jobs])
    executable = work / "cmake/bin" / ("Release/ps2_recomp.exe" if os.name == "nt" else "ps2_recomp")
    if not executable.is_file():
        raise RuntimeError(f"Missing expected compiler: {executable}")
    # The real CLI prints usage and exits 1 when no game config is supplied.
    smoke = subprocess.run([str(executable)], capture_output=True, text=True)
    if smoke.returncode != 1 or "Usage:" not in smoke.stdout:
        raise RuntimeError("Compiler CLI usage check failed")
    print(smoke.stdout, end="")
    (work / "result.json").write_text(
        json.dumps(
            {
                "executable": str(executable),
                "sha256": digest(executable),
                "source": str(source),
                "platform": platform.system(),
                "machine": platform.machine(),
                "recipe_sha256": digest(marker),
            },
            indent=2,
        )
        + "\n"
    )
    print(f"Tool-only build complete: {executable}")


if __name__ == "__main__":
    try:
        main()
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        sys.exit(1)
