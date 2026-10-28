#!/usr/bin/env python3
"""Package the builder for people with their own ISO: a zip kit, or with --installer a DrakanPC-Setup.exe.

Both are a snapshot of a Git revision (HEAD by default; uncommitted changes are not included). Every source file passes
the repository guard, so neither holds game data, generated game C++ or the private VU image; the recipient builds the
game from their own disc with Build-Drakan.cmd. The installer also bundles what a non-technical user would otherwise
have to install (pinned in kit/installer.lock.json): portable PowerShell 7 and Python, the prebuilt ps2_recomp.exe
(built from the same snapshot) and the raylib / FFmpeg sources, so the build needs no Git, no Python and no downloads
other than Microsoft's C++ Build Tools, which Build-Drakan.ps1 installs on first run.
"""

import argparse
import hashlib
import importlib.util
import io
import json
import os
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
KIT = HERE / "kit"
# Developer-only material the build never reads.
EXCLUDED_PREFIXES = ("docs/journal/", "scripts/archive/", ".github/", ".githooks/")
EXCLUDED_FILES = {"AGENTS.md", "CLAUDE.md"}
# Files placed at the kit root, next to BUILDING.md.
KIT_ROOT_FILES = {"Build-Drakan.cmd": KIT / "Build-Drakan.cmd", "README.txt": KIT / "README.txt"}
INSTALLER_ROOT_FILES = {"SETUP-INFO.txt": KIT / "SETUP-INFO.txt"}
ICON = "ps2recomp/ps2xRuntime/src/res/drakan.ico"


def load_guard():
    spec = importlib.util.spec_from_file_location("repository_guard", REPO / "scripts/repository_guard.py")
    guard = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(guard)
    return guard


def git(*args, cwd=REPO):
    return subprocess.check_output(["git", "-C", str(cwd), *args])


def run(args, cwd=None):
    print("+ " + subprocess.list2cmdline([str(a) for a in args]), flush=True)
    subprocess.run([str(a) for a in args], cwd=cwd, check=True)


def snapshot(revision):
    """Yield (path, bytes) for every file of the revision, line endings as a checkout would have them."""
    with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", revision))) as archive:
        for member in archive:
            if member.isfile():
                yield member.name, archive.extractfile(member).read()
            elif not member.isdir() and member.name != "pax_global_header":
                raise RuntimeError(f"{member.name}: links and special files are not packaged")


def select(entries, guard):
    kept, blocked = {}, []
    for name, data in entries:
        if name.startswith(EXCLUDED_PREFIXES) or name in EXCLUDED_FILES:
            continue
        why = guard.reason(name, data)
        if why:
            blocked.append(f"{name}: {why}")
        else:
            kept[name] = data
    if blocked:
        raise RuntimeError("Files rejected by the repository guard:\n  " + "\n  ".join(blocked))
    return kept


def kit_entries(commit):
    entries = select(snapshot(commit), load_guard())
    for name, source in KIT_ROOT_FILES.items():
        if name in entries:
            raise RuntimeError(f"{name} already exists at the repository root")
        entries[name] = source.read_bytes()
    entries["KIT-VERSION.txt"] = f"commit {commit}\n".encode()
    for required in ("CMakeLists.txt", "configs/drakan.toml", "scripts/buildtool/Build-Drakan.ps1"):
        if required not in entries:
            raise RuntimeError(f"{commit[:12]} lacks {required}; commit the builder kit first")
    return entries


def write_zip(path, prefix, entries):
    with zipfile.ZipFile(path, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(f"{prefix}/{name}", date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            archive.writestr(info, data)


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(spec, cache):
    """Fetch a pinned archive into the cache once; refuse anything whose SHA-256 differs from the lock."""
    target = cache / spec["url"].rsplit("/", 1)[1]
    if not target.is_file():
        print(f"Downloading {spec['url']}", flush=True)
        partial = target.with_suffix(".part")
        with urllib.request.urlopen(spec["url"]) as response, open(partial, "wb") as stream:
            shutil.copyfileobj(response, stream)
        partial.replace(target)
    if sha256(target) != spec["sha256"]:
        raise RuntimeError(f"{target.name} does not match the SHA-256 pinned in installer.lock.json")
    return target


def fetch_raylib(spec, cache):
    target = cache / f"raylib-{spec['commit'][:12]}"
    if not target.exists():
        run(["git", "init", "-q", target])
        run(["git", "-C", target, "fetch", "-q", "--depth=1", spec["url"], spec["commit"]])
        run(["git", "-C", target, "checkout", "-q", "--detach", "FETCH_HEAD"])
    if git("rev-parse", "HEAD", cwd=target).decode().strip() != spec["commit"]:
        raise RuntimeError(f"Unexpected raylib checkout at {target}; delete it")
    return target


def build_compiler(stage, commit, work_root):
    """Build ps2_recomp.exe from the staged snapshot; reused while ps2xRecomp and the lock are unchanged."""
    key = hashlib.sha256(
        git("rev-parse", f"{commit}:ps2recomp/ps2xRecomp", f"{commit}:scripts/buildtool/toolchain.lock.json")
    ).hexdigest()[:16]
    work = work_root / key
    result = work / "result.json"
    if not result.is_file():
        run([sys.executable, stage / "scripts/buildtool/build_tool.py", "--work-dir", work, "--jobs", "4"])
    return Path(json.loads(result.read_text(encoding="utf-8"))["executable"])


def find_iscc(explicit):
    candidates = [explicit, shutil.which("ISCC")]
    for variable, folder in (("LOCALAPPDATA", "Programs"), ("ProgramFiles(x86)", ""), ("ProgramFiles", "")):
        if os.environ.get(variable):
            candidates.append(str(Path(os.environ[variable]) / folder / "Inno Setup 6/ISCC.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError("Inno Setup 6 is required for --installer (winget install JRSoftware.InnoSetup) or pass --iscc")


def build_installer(entries, commit, output_directory, iscc):
    lock = json.loads((KIT / "installer.lock.json").read_text(encoding="utf-8"))
    work_root = output_directory / "work"
    cache = work_root / "cache"
    cache.mkdir(parents=True, exist_ok=True)
    stage = work_root / "stage"
    if stage.exists():
        shutil.rmtree(stage)
    for name, data in entries.items():
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
    for name, source in INSTALLER_ROOT_FILES.items():
        shutil.copyfile(source, stage / name)
    shutil.copyfile(stage / ICON, stage / "drakan.ico")
    # Build-LocalGame.ps1 uses the compiler next to it instead of building one (which would need Git).
    shutil.copyfile(build_compiler(stage, commit, work_root / "compiler"), stage / "scripts/buildtool/ps2_recomp.exe")
    # Third-party sources for the runtime build: Build-LocalGame.ps1 picks up <root>/deps by itself.
    raylib = fetch_raylib(lock["raylib"], cache)
    shutil.copytree(raylib, stage / "deps/raylib", ignore=shutil.ignore_patterns(".git", "examples"))
    shutil.copyfile(download(lock["ffmpeg"], cache), stage / "deps" / lock["ffmpeg"]["url"].rsplit("/", 1)[1])
    for tool in ("python", "pwsh"):
        with zipfile.ZipFile(download(lock[tool], cache)) as archive:
            archive.extractall(stage / "tools" / tool)
    smoke = subprocess.run(
        [stage / "tools/python/python.exe", stage / "scripts/buildtool/prepare_iso.py", "--help"], capture_output=True
    )
    if smoke.returncode != 0:
        raise RuntimeError("The bundled Python cannot run the build scripts")
    version = f"1.0.{git('rev-list', '--count', commit).decode().strip()}"
    name = f"DrakanPC-Setup-{commit[:12]}"
    run(
        [
            iscc,
            "/Q",
            f"/DStageDir={stage}",
            f"/DAppVersion={version}",
            f"/DOutputDir={output_directory}",
            f"/DOutputName={name}",
            KIT / "DrakanPC-Setup.iss",
        ]
    )
    return output_directory / f"{name}.exe"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="HEAD", help="Git revision to package (default HEAD)")
    parser.add_argument("--output-directory", type=Path, default=REPO / "build/builder-kit")
    parser.add_argument("--name", default="DrakanPC-Builder", help="top-level folder inside the zip")
    parser.add_argument("--installer", action="store_true", help="build DrakanPC-Setup.exe instead of the zip")
    parser.add_argument("--iscc", help="path to Inno Setup's ISCC.exe")
    args = parser.parse_args()
    commit = git("rev-parse", "--verify", f"{args.revision}^{{commit}}").decode().strip()
    if args.revision == "HEAD" and git("status", "--porcelain", "--untracked-files=no").strip():
        print("Note: the working tree has uncommitted changes; the package contains the committed HEAD only.")
    entries = kit_entries(commit)
    output_directory = args.output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    if args.installer:
        output = build_installer(entries, commit, output_directory, find_iscc(args.iscc))
    else:
        output = output_directory / f"{args.name}-{commit[:12]}.zip"
        write_zip(output, args.name, entries)
    digest = sha256(output)
    manifest = {"file": output.name, "commit": commit, "source_files": len(entries), "sha256": digest}
    output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Package: {output} ({output.stat().st_size / 2**20:.1f} MiB)")
    print(f"SHA-256: {digest}")


if __name__ == "__main__":
    main()
