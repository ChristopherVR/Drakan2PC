#!/usr/bin/env python3
"""Package the compiler and corresponding source; never package the game."""

import argparse
import hashlib
import json
import os
import subprocess
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
SOURCE_EXTENSIONS = {
    ".c",
    ".cpp",
    ".cc",
    ".cxx",
    ".h",
    ".hpp",
    ".hh",
    ".inc",
    ".in",
    ".cmake",
    ".txt",
    ".md",
    ".rst",
    ".py",
    ".sh",
    ".bat",
    ".ps1",
    ".json",
    ".yml",
    ".yaml",
    ".toml",
    ".def",
    ".map",
    ".pc",
    ".ac",
    ".am",
    ".m4",
}
# The source projection is the root CMake project restricted to the recompiler (DRAKAN_BUILD_RECOMPILER); the archive
# root mirrors the repository root.
RUNTIME_FILES = {
    "ps2recomp/ps2xRuntime/include/ps2_runtime_calls.h",
    "ps2recomp/ps2xRuntime/include/runtime/ps2_address.h",
}


def files(repo, wanted=lambda path: True):
    data = subprocess.check_output(
        ["git", "-C", str(repo), "ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    )
    for raw in sorted(set(data.split(b"\0")) - {b""}):
        path = raw.decode("utf-8")
        if not wanted(path):
            continue
        candidate = repo / path
        if not candidate.is_file() or candidate.is_symlink():
            continue
        # The compiler source projection never includes runtime/aot or resources.
        if "aot" in candidate.parts or candidate.name.startswith("ps2_recompiled"):
            continue
        name = candidate.name.upper()
        if candidate.suffix.lower() not in SOURCE_EXTENSIONS and not name.startswith(
            ("LICENSE", "COPYING", "COPYRIGHT", "NOTICE", "AUTHORS", "MAKEFILE", "CMAKELISTS")
        ):
            continue
        data = candidate.read_bytes()
        if b"kVu1Image[" in data or data.startswith((b"\x7fELF", b"MZ")):
            raise RuntimeError(f"Unexpected embedded image/binary in compiler source: {candidate}")
        yield path, data


def write_zip(path, entries):
    with zipfile.ZipFile(path, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = (0o755 if name == "ps2_recomp" else 0o644) << 16
            archive.writestr(info, data)


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--work-dir", type=Path, required=True)
    ap.add_argument("--output-directory", type=Path, required=True)
    args = ap.parse_args()
    work, out = args.work_dir.resolve(), args.output_directory.resolve()
    recipe = json.loads((work / "recipe.json").read_text())
    result = json.loads((work / "result.json").read_text())
    binary = Path(result["executable"])
    binary_data = binary.read_bytes()
    if hashlib.sha256(binary_data).hexdigest() != result["sha256"]:
        raise RuntimeError("Compiler hash changed after build")
    source = Path(result["source"])
    entries = {}

    def wanted(name):
        return (
            name.startswith(("ps2recomp/ps2xRecomp/", "cmake/"))
            or name in RUNTIME_FILES
            or name in ("CMakeLists.txt", "ps2recomp/LICENSE")
        )

    for name, data in files(source, wanted):
        entries["PS2Recomp/" + name] = data
    for dependency in recipe["lock"]["dependencies"]:
        for name, data in files(work / "deps" / dependency):
            entries[f"deps/{dependency}/{name}"] = data
    if "PS2Recomp/ps2recomp/LICENSE" not in entries:
        raise RuntimeError("Missing upstream GPL license")
    rebuild = (HERE / "rebuild_sources.py").read_bytes()
    entries["rebuild_sources.py"] = rebuild
    entries["recipe.json"] = json.dumps(recipe, indent=2).encode()
    source_name = "ps2-recomp-corresponding-source.zip"
    notices = (
        "PS2Recomp compiler: GPL-3.0; upstream license included.\n"
        "Corresponding compiler and dependency source is in " + source_name + ".\n"
        "Extract it and run python rebuild_sources.py with CMake, Git and a C++20 toolchain.\n"
        "Dependency license texts remain in the source archive at their original paths.\n"
        "No game executable, generated game C++, ISO, textures or NVIDIA components are included.\n"
        "The compiler accepts a local configuration; it is not a playable game or emulator installer.\n"
    )
    out.mkdir(parents=True, exist_ok=True)
    write_zip(out / source_name, entries)
    manifest = {
        "platform": result["platform"],
        "machine": result["machine"],
        "compiler_sha256": result["sha256"],
        "recipe": recipe,
    }
    tool_entries = {
        binary.name: binary_data,
        "LICENSE-PS2Recomp.txt": (source / "ps2recomp/LICENSE").read_bytes(),
        "NOTICES.txt": notices.encode(),
        "manifest.json": json.dumps(manifest, indent=2).encode(),
        source_name: (out / source_name).read_bytes(),
    }
    # Windows builds use the static MSVC runtime. Do not copy arbitrary DLLs.
    if os.name == "nt":
        tool_entries["Build-LocalGame.ps1"] = (HERE / "Build-LocalGame.ps1").read_bytes()
        tool_entries["prepare_iso.py"] = (HERE / "prepare_iso.py").read_bytes()
        tool_entries["prepare_launch.py"] = (HERE / "prepare_launch.py").read_bytes()
        tool_entries["toolchain.lock.json"] = (HERE / "toolchain.lock.json").read_bytes()
        tool_entries["build_tool.py"] = (HERE / "build_tool.py").read_bytes()
    name = "ps2-recomp-" + result["platform"].lower() + "-x64.zip"
    write_zip(out / name, tool_entries)
    sums = "".join(hashlib.sha256(p.read_bytes()).hexdigest() + "  " + p.name + "\n" for p in sorted(out.glob("*.zip")))
    (out / "SHA256SUMS.txt").write_text(sums, encoding="ascii")
    print(f"Compiler-only package: {out / name}")


if __name__ == "__main__":
    main()
