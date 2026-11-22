#!/usr/bin/env python3
"""Package the builder for people with their own ISO: a zip kit, or with --installer a DrakanPC-Setup.exe.

Both are a snapshot of a Git revision (HEAD by default; uncommitted changes are not included). Every source file passes
the repository guard, so neither holds game data, generated game C++ or the private VU image; the recipient builds the
game from their own disc with Build-Drakan.cmd. Both also carry the prebuilt ps2_recomp.exe (built from the same
snapshot) and the raylib / FFmpeg sources (pinned in kit/installer.lock.json), so neither build needs Git. The installer
also bundles portable PowerShell 7, Python and Node.js. The ZIP uses the player's own PowerShell, Python and Node.js.
Both kits include fflate for the Order of the Flame converter.
"""

import argparse
import hashlib
import importlib.util
import io
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[2] / "analysis/scripts"))
from node_runtime import node_environment

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
KIT = HERE / "kit"
INSTALLER_LOCK = "scripts/buildtool/kit/installer.lock.json"
RELEASE_MARKER = "DrakanPC-release.txt"
RELEASE_TAG = re.compile(r"v(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)\.(?:0|[1-9][0-9]*)")
# The Windows and browser builds, optional extras, and bundled recompiler share this source payload.
BUILDER_DIRECTORIES = (
    "cmake/",
    "configs/regions/",
    "editor/packages/editor-core/schema/",
    "editor/packages/editor-core/src/",
    "editor/packages/formats/src/",
    "licenses/",
    "ps2recomp/ps2xIOP/include/",
    "ps2recomp/ps2xIOP/src/",
    "ps2recomp/ps2xRecomp/include/",
    "ps2recomp/ps2xRecomp/src/",
    "ps2recomp/ps2xRuntime/cmake/",
    "ps2recomp/ps2xRuntime/include/",
    "ps2recomp/ps2xRuntime/src/",
    "runtime/",
    "scripts/buildtool/kit/art/maps/",
    "scripts/buildtool/kit/art/settings/",
    "scripts/buildtool/kit/models/",
    "scripts/buildtool/kit/subtitles/",
    "scripts/launchers/",
    "third_party/fonts/",
    "third_party/imgui/",
)
BUILDER_FILES = {
    "BUILDING.md",
    "CMakeLists.txt",
    "CMakePresets.json",
    "LICENSE",
    "LICENSES.md",
    "README.md",
    "THIRD_PARTY.md",
    "analysis/functions.csv",
    "analysis/jumptables.csv",
    "analysis/regions/usa/functions.csv",
    "analysis/regions/usa/jumptables.csv",
    *(
        "analysis/scripts/" + name
        for name in (
            "character_motion.py",
            "character_motion_retarget.ts",
            "character_previews.py",
            "coop_character_roster.py",
            "hd_pack.py",
            "iop_mangle.py",
            "iop_srsc.py",
            "lvl_dump.py",
            "lvl_objects.py",
            "node_runtime.py",
            "material_maps.py",
            "odb_fields.py",
            "ootf_cab.py",
            "ootf_hd_pack.py",
            "ootf_ps2_data.py",
            "ootf_subtitles.py",
            "title_cover.py",
            "txd_decode.py",
            "ui_layout_dump.py",
            "xdb_dump.py",
        )
    ),
    "configs/drakan-usa.toml",
    "configs/drakan.toml",
    "configs/mp_maps.ini",
    "docs/guides/saves.md",
    "docs/reference/level-format.md",
    "docs/reference/regions.md",
    "editor/package.json",
    "editor/packages/editor-core/package.json",
    "editor/packages/formats/LICENSE",
    "editor/packages/formats/package.json",
    "editor/scripts/ootf-convert.ts",
    "editor/scripts/ootf-speech.ts",
    "ps2recomp/LICENSE",
    "ps2recomp/ps2xIOP/CMakeLists.txt",
    "ps2recomp/ps2xRecomp/CMakeLists.txt",
    "ps2recomp/ps2xRuntime/CMakeLists.txt",
    "runtime/fmv/README.md",
    *(
        "scripts/buildtool/" + name
        for name in (
            "powershell/Build-Affinity.ps1",
            "Build-Drakan.ps1",
            "Build-Drakan.sh",
            "powershell/Build-Job.ps1",
            "Build-LocalGame.ps1",
            "powershell/Build-Policy.ps1",
            "powershell/Build-Progress.ps1",
            "Build-Recovery.ps1",
            "powershell/Build-Textures.ps1",
            "apply_defaults.py",
            "bootstrap_linux.py",
            "drakan_update_linux.py",
            "build-messages.json",
            "build_tool.py",
            "dlss.lock.json",
            "extras.py",
            "fetch_dlss_sdk.py",
            "generate_runtime_profile.py",
            "install_ootf_save_migrations.py",
            "install_private_mods.py",
            "kit/art/d2-cover.png",
            "kit/art/ootf-cover.png",
            "migrate_saves.py",
            "prepare_iso.py",
            "prepare_launch.py",
            "prepare_web_deps.py",
            "regions.json",
            "serve_web.py",
            "stage_texture_pack.py",
            "toolchain.lock.json",
            "web-toolchain.lock.json",
        )
    ),
    *(
        "scripts/harness/" + name
        for name in (
            "dual_wield_check.py",
            "map_previews.py",
            "netplay_two_instances.py",
            "pack_map_previews.py",
            "play_run.py",
            "setting_shots.py",
        )
    ),
    "scripts/prepare_local_runtime.py",
    "third_party/wgpu-native/LICENSE.APACHE",
    "third_party/wgpu-native/LICENSE.MIT",
    INSTALLER_LOCK,
}
DEVELOPER_FILES = {
    "runtime/vu1ref/recorder_hook.txt",
    "runtime/vu1ref/vu1ref_dumptest.cpp",
    "runtime/vu1ref/vu1ref_replay.cpp",
    "runtime/vu1ref/vu1ref_ringscan.cpp",
    "third_party/imgui/fonts/binary_to_compressed_c.cpp",
}


def excluded(name):
    """True for repository files the builder kit leaves out."""
    if name in BUILDER_FILES:
        return False
    if not name.startswith(BUILDER_DIRECTORIES) or name in DEVELOPER_FILES:
        return True
    if (
        name.startswith(("runtime/web/pages/", "runtime/fmv/tools/"))
        or "/tests/" in name
        or name.endswith("/README.md")
    ):
        return True
    return any(part.startswith(".") for part in name.split("/"))


# Files placed at the kit root, next to BUILDING.md.
KIT_ROOT_FILES = {"Build-Drakan.cmd": KIT / "Build-Drakan.cmd", "README.txt": KIT / "locales/en/README.txt"}
LOCALIZED_READMES = {
    f"readme/{locale}.txt": KIT / "locales" / locale / "README.txt" for locale in ("de", "fr", "it", "es")
}
# The builders whose Python scripts the installer smoke test runs, and the Node.js script step 6 runs.
BUILDERS = ("scripts/buildtool/Build-LocalGame.ps1", "scripts/buildtool/Build-Drakan.ps1")
RETARGET = "analysis/scripts/character_motion_retarget.ts"
EXTRAS = "scripts/buildtool/extras.py"


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


def archive_files(revision, cwd=REPO, prefix=""):
    with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", revision, cwd=cwd))) as archive:
        for member in archive:
            if member.isfile():
                yield prefix + member.name, archive.extractfile(member).read()
            elif not member.isdir() and member.name != "pax_global_header":
                raise RuntimeError(f"{member.name}: links and special files are not packaged")


def snapshot(revision):
    """Yield (path, bytes) for every file of the revision and of its submodules, line endings as a checkout has them.

    git archive leaves submodules out; character_motion.py needs the editor/packages/formats sources at build time.
    """
    yield from archive_files(revision)
    for line in git("ls-tree", "-r", "-z", revision).decode().split("\0"):
        if line.startswith("160000 "):
            meta, path = line.split("\t", 1)
            commit = meta.split()[2]
            if not (REPO / path / ".git").exists():
                raise RuntimeError(f"Submodule {path} is not checked out (git submodule update --init)")
            yield from archive_files(commit, cwd=REPO / path, prefix=f"{path}/")


def select(entries, guard):
    kept, blocked = {}, []
    for name, data in entries:
        if excluded(name):
            continue
        why = guard.reason(name, data)
        if why:
            blocked.append(f"{name}: {why}")
        else:
            kept[name] = data
    if blocked:
        raise RuntimeError("Files rejected by the repository guard:\n  " + "\n  ".join(blocked))
    return kept


def kit_entries(commit, *, release_tag=None):
    if release_tag is not None and not RELEASE_TAG.fullmatch(release_tag):
        raise ValueError("Release tag must be a stable vX.Y.Z version")
    entries = select(snapshot(commit), load_guard())
    for name, source in (KIT_ROOT_FILES | LOCALIZED_READMES).items():
        if name in entries:
            raise RuntimeError(f"{name} already exists at the repository root")
        entries[name] = source.read_bytes()
    entries["KIT-VERSION.txt"] = f"commit {commit}\n".encode()
    # An unversioned kit clears any release tag left by a previous install over the same builder folder.
    entries[RELEASE_MARKER] = f"{release_tag}\n".encode("ascii") if release_tag else b""
    for required in ("CMakeLists.txt", "configs/drakan.toml", "scripts/buildtool/Build-Drakan.ps1", INSTALLER_LOCK):
        if required not in entries:
            raise RuntimeError(f"{commit[:12]} lacks {required}; commit the builder kit first")
    return entries


def write_zip(path, prefix, entries):
    """Entries map a kit path to its bytes, or to a file on disk (the prebuilt files, streamed)."""
    with zipfile.ZipFile(path, "x", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for name, data in sorted(entries.items()):
            info = zipfile.ZipInfo(f"{prefix}/{name}", date_time=(1980, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            if isinstance(data, bytes):
                archive.writestr(info, data)
            else:
                with open(data, "rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target, 1 << 20)


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


def extract_tool(archive_path, target, *, installer_tool=None):
    """Unpack a portable tool, dropping a single wrapper folder such as Node.js uses."""
    with zipfile.ZipFile(archive_path) as archive:
        members = [member for member in archive.infolist() if not member.is_dir()]
        roots = {member.filename.partition("/")[:2] for member in members}
        root, slash = next(iter(roots)) if len(roots) == 1 else ("", "")
        wrapper = root + slash if slash else ""
        archive.extractall(
            target,
            [
                member
                for member in members
                if installer_tool is None or installer_tool_file(installer_tool, member.filename.removeprefix(wrapper))
            ],
        )
    if wrapper:
        inner = target / root
        for child in inner.iterdir():
            child.rename(target / child.name)
        inner.rmdir()


def builder_scripts(root):
    """Every Python script Build-LocalGame.ps1 / Build-Drakan.ps1 run, as paths under root."""
    found = set()
    for builder in BUILDERS:
        text = (root / builder).read_text(encoding="utf-8")
        for name in re.findall(r"'([\w/]+\.py)'", text):
            for base in ("", "scripts/buildtool/", "scripts/"):
                if (root / base / name).is_file():
                    found.add((root / base / name).resolve())
                    break
    return sorted(found)


def first_error(text):
    lines = text.strip().splitlines()
    return next((line.strip() for line in lines if "Error" in line), lines[-1] if lines else "no output")


def fetch_tools(target):
    """Unpack the pinned embeddable Python (with its ._pth as shipped) and Node.js into target/python and target/node."""
    lock = json.loads((KIT / "installer.lock.json").read_text(encoding="utf-8"))
    cache = target / "cache"
    cache.mkdir(parents=True, exist_ok=True)
    for tool in ("python", "node"):
        archive = download(lock[tool], cache)
        stamp = target / tool / ".source"
        if not stamp.is_file() or stamp.read_text(encoding="utf-8") != archive.name:
            if (target / tool).exists():
                shutil.rmtree(target / tool)
            extract_tool(archive, target / tool)
            stamp.write_text(archive.name, encoding="utf-8")
    return target


def smoke_test(stage, tools=None, node=True):
    """Run every builder script (and the Node part of step 6) with the bundled tools, as the kit will."""
    tools = tools or stage / "tools"
    python = tools / "python/python.exe"
    failures = []
    for script in builder_scripts(stage):
        result = subprocess.run([python, "-B", script, "--help"], capture_output=True, text=True, cwd=stage)
        if result.returncode != 0:
            failures.append(f"{script.relative_to(stage).as_posix()}: {first_error(result.stderr)}")
    # The optional extras (Order of the Flame, HD textures) only use the standard library; their time estimate must run.
    result = subprocess.run(
        [python, "-B", stage / EXTRAS, "estimate", "--ootf", "--hd", "--gpu", "NVIDIA GeForce RTX 4070"],
        capture_output=True,
        text=True,
        cwd=stage,
    )
    if result.returncode != 0 or "Total" not in result.stdout:
        failures.append(f"{EXTRAS}: {first_error(result.stderr)}")
    if node:
        # With no arguments the script stops after its imports resolved; a missing module fails before that.
        result = subprocess.run(
            [tools / "node/node.exe", stage / RETARGET],
            capture_output=True,
            text=True,
            cwd=stage,
            env=node_environment(),
        )
        if "scratch and output directories required" not in result.stderr:
            failures.append(f"{RETARGET}: {first_error(result.stderr)}")
    if failures:
        raise RuntimeError("The bundled tools cannot run the build scripts:\n  " + "\n  ".join(failures))


def find_iscc(explicit):
    candidates = [explicit, shutil.which("ISCC")]
    for variable, folder in (("LOCALAPPDATA", "Programs"), ("ProgramFiles(x86)", ""), ("ProgramFiles", "")):
        if os.environ.get(variable):
            candidates.append(str(Path(os.environ[variable]) / folder / "Inno Setup 6/ISCC.exe"))
    for candidate in candidates:
        if candidate and Path(candidate).is_file():
            return candidate
    raise RuntimeError("Inno Setup 6 is required for --installer (winget install JRSoftware.InnoSetup) or pass --iscc")


def write_stage(entries, stage):
    """The snapshot as files under a fresh stage folder (what the compiler build and the installer read)."""
    if stage.exists():
        shutil.rmtree(stage)
    for name, data in entries.items():
        target = stage / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)


def ffmpeg_files(spec, cache):
    archive_path = download(spec, cache)
    if "kit_dlls" not in spec:
        # Older snapshots still expect the original archive.
        return {f"deps/{archive_path.name}": archive_path}
    libraries = ("avcodec", "avformat", "avutil", "swresample", "swscale")
    binaries = {f"bin/{name}.lib" for name in libraries} | {f"bin/{name}" for name in spec["kit_dlls"]}
    headers = tuple(f"include/lib{name}/" for name in libraries)
    target = cache / archive_path.stem
    with zipfile.ZipFile(archive_path) as archive:
        names = {member.filename for member in archive.infolist() if not member.is_dir()}
        if missing := binaries - names:
            raise RuntimeError(f"{archive_path.name} lacks {', '.join(sorted(missing))}")
        kept = binaries | {name for name in names if name.startswith((*headers, "licenses/"))}
        archive.extractall(target, sorted(kept))
    return {f"deps/ffmpeg/{name}": target / name for name in kept}


def prebuilt_files(stage, commit, work_root, lock):
    """What lets either kit build without Git, as kit path -> file: ps2_recomp.exe built from the staged snapshot
    (Build-LocalGame.ps1 uses the compiler next to it instead of building one) and the raylib source and FFmpeg prebuilt
    (Build-LocalGame.ps1 picks up <root>/deps by itself), plus fflate for the optional converter."""
    cache = work_root / "cache"
    cache.mkdir(parents=True, exist_ok=True)
    files = {"scripts/buildtool/ps2_recomp.exe": build_compiler(stage, commit, work_root / "compiler")}
    raylib = fetch_raylib(lock["raylib"], cache)
    for path in sorted(raylib.rglob("*")):
        relative = path.relative_to(raylib)
        if relative.parts[0] not in {
            "src",
            "cmake",
            "CMakeLists.txt",
            "CMakeOptions.txt",
            "LICENSE",
            "README.md",
            "raylib.pc.in",
        }:
            continue
        if path.is_file() and not any(part.startswith(".") for part in relative.parts):
            files[f"deps/raylib/{relative.as_posix()}"] = path
    files.update(ffmpeg_files(lock["ffmpeg"], cache))
    fflate = download(lock["hd"]["fflate"], cache)
    files[f"deps/{fflate.name}"] = fflate
    return files


def build_zip(entries, commit, output_directory, name):
    lock = json.loads(entries[INSTALLER_LOCK])
    work_root = output_directory / "work"
    stage = work_root / "zip-stage"
    write_stage(entries, stage)
    files = dict(entries)
    for path, source in prebuilt_files(stage, commit, work_root, lock).items():
        if path in files:
            raise RuntimeError(f"{path} is both in the snapshot and a prebuilt file")
        files[path] = source
    output = output_directory / f"{name}-{commit[:12]}.zip"
    write_zip(output, name, files)
    return output


def installer_tool_file(tool, name):
    if tool == "node":
        # The converter imports pinned dependencies itself and runs TypeScript directly with Node.
        return name in {"node.exe", "LICENSE"}
    if tool == "python":
        return name != "pythonw.exe"
    # PowerShell's root XML files document its .NET API. Its deployment scripts configure the host rather than run
    # the builder. Keep modules, reference assemblies (Add-Type), localized resources and notices.
    if "/" not in name:
        return not name.endswith((".xml", ".ps1", ".adml", ".admx"))
    return name != "preview/pwsh-preview.cmd"


def build_installer(entries, commit, output_directory, iscc, *, release_tag=None):
    lock = json.loads(entries[INSTALLER_LOCK])
    work_root = output_directory / "work"
    cache = work_root / "cache"
    stage = work_root / "stage"
    write_stage(entries, stage)
    for path, source in prebuilt_files(stage, commit, work_root, lock).items():
        (stage / path).parent.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(source, stage / path)
    for tool in ("python", "pwsh", "node"):
        extract_tool(download(lock[tool], cache), stage / "tools" / tool, installer_tool=tool)
    smoke_test(stage)
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
            *([f"/DReleaseTag={release_tag}", f"/DReleaseProductVersion={release_tag[1:]}.0"] if release_tag else []),
            KIT / "DrakanPC-Setup.iss",
        ]
    )
    return output_directory / f"{name}.exe"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--revision", default="HEAD", help="Git revision to package (default HEAD)")
    parser.add_argument("--release-tag", help="public stable vX.Y.Z tag for the running game's update check")
    parser.add_argument("--output-directory", type=Path, default=REPO / "build/builder-kit")
    parser.add_argument("--name", default="DrakanPC-Builder", help="top-level folder inside the zip")
    parser.add_argument("--installer", action="store_true", help="build DrakanPC-Setup.exe instead of the zip")
    parser.add_argument("--iscc", help="path to Inno Setup's ISCC.exe")
    parser.add_argument(
        "--fetch-tools",
        type=Path,
        metavar="DIR",
        help="only unpack the pinned embeddable Python and Node.js into DIR (for scripts/tests/test_kit_tools.py)",
    )
    args = parser.parse_args()
    if args.installer and args.release_tag:
        if not RELEASE_TAG.fullmatch(args.release_tag) or any(
            len(part) > 5 or int(part) > 65535 for part in args.release_tag[1:].split(".")
        ):
            parser.error("a versioned installer needs a stable release tag with each version component at most 65535")
    if args.fetch_tools:
        print(f"Kit tools: {fetch_tools(args.fetch_tools.resolve())}")
        return
    commit = git("rev-parse", "--verify", f"{args.revision}^{{commit}}").decode().strip()
    if args.revision == "HEAD" and git("status", "--porcelain", "--untracked-files=no").strip():
        print("Note: the working tree has uncommitted changes; the package contains the committed HEAD only.")
    entries = kit_entries(commit, release_tag=args.release_tag)
    output_directory = args.output_directory.resolve()
    output_directory.mkdir(parents=True, exist_ok=True)
    if args.installer:
        output = build_installer(
            entries,
            commit,
            output_directory,
            find_iscc(args.iscc),
            release_tag=args.release_tag,
        )
    else:
        output = build_zip(entries, commit, output_directory, args.name)
    digest = sha256(output)
    manifest = {"file": output.name, "commit": commit, "source_files": len(entries), "sha256": digest}
    output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Package: {output} ({output.stat().st_size / 2**20:.1f} MiB)")
    print(f"SHA-256: {digest}")


if __name__ == "__main__":
    main()
