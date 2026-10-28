#!/usr/bin/env python3
"""Rebuild the corresponding-source archive without fetching dependencies."""

import os
import subprocess
from pathlib import Path

root = Path(__file__).resolve().parent
args = [
    "cmake",
    "-S",
    str(root / "PS2Recomp"),
    "-B",
    str(root / "build"),
    "-DCMAKE_BUILD_TYPE=Release",
    "-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded",
    "-DLIBDWARF_CRT=MT",
    "-DDRAKAN_BUILD_RECOMPILER=ON",
    "-DDRAKAN_BUILD_RUNTIME_TESTS=OFF",
    "-DDRAKAN_BUILD_GAME=OFF",
    "-DDRAKAN_BUILD_PS2X_TESTS=OFF",
    "-DBUILD_TESTING=OFF",
    "-DENABLE_DECOMPRESSION=OFF",
]
args += [
    f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={(root / 'deps' / name).as_posix()}"
    for name in ("elfio", "toml11", "fmt", "libdwarf", "rabbitizer")
]
args += ["-G", "Visual Studio 17 2022", "-A", "x64"] if os.name == "nt" else ["-G", "Ninja"]
subprocess.run(args, check=True)
subprocess.run(
    ["cmake", "--build", str(root / "build"), "--config", "Release", "--target", "ps2_recomp", "--parallel", "2"],
    check=True,
)
