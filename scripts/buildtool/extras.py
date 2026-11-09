#!/usr/bin/env python3
"""Optional extras of the guided build: Drakan: Order of the Flame and HD textures, both made from the player's own files.

Build-Drakan.ps1 (and so DrakanPC-Setup.exe) calls this with the standard library only, under the kit's embeddable Python.

    extras.py estimate --ootf --hd [--gpu NAME]         what each extra costs in time and disk on this PC (an estimate)
    extras.py ootf --iso ISO --source SRC               convert Order of the Flame into build/ootf/mods/ootf-levels.zip
    extras.py hd --iso ISO --play DIR [--ootf-pack ZIP] build the HD texture pack into <play folder>/textures
    extras.py check-source SRC                          what kind of Drakan 1 source is this (install, disc folder, image)

Order of the Flame (Drakan 1, PC) can be given as its install folder, a folder with the disc's SETUP/DATA1.CAB, a disc
image (.mdx / .iso) or the .zip that holds one. The converter (editor/scripts/ootf-convert.ts) needs the Drakan 2 disc's
level template, which is read from the ISO already chosen for the build. HD textures come from Real-ESRGAN
(realesrgan-ncnn-vulkan, any Vulkan GPU) over every texture of the disc and, when chosen, of the converted pack.

Nothing here is uploaded or committed: the converted pack and the HD textures are derived from files the player owns and stay
on this PC (build/, tools/ and the play folder are ignored). Downloads (Real-ESRGAN, numpy, Pillow, fflate) are pinned by
SHA-256 in scripts/buildtool/kit/installer.lock.json and refused when they differ.
"""

from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from dataclasses import dataclass
from pathlib import Path, PurePosixPath

ROOT = Path(__file__).resolve().parents[2]
LOCK = ROOT / "scripts/buildtool/kit/installer.lock.json"
ANALYSIS = ROOT / "analysis/scripts"
TEMPLATE = "LEVEL1NA"
PACK_ID = "ootf-levels"
CONVERTER = "editor/scripts/ootf-convert.ts"


class ExtrasError(RuntimeError):
    """A failure the player can act on; the message says how."""


# --------------------------------------------------------------------------------------------------------- estimates
@dataclass(frozen=True)
class Step:
    key: str
    title: str
    low: float  # minutes on the reference PC
    high: float
    gpu: bool = False  # scales with the graphics card
    once: bool = False  # only the first time (downloads)
    basis: str = ""  # where the figure comes from


REFERENCE_GPU = "NVIDIA GeForce RTX 5080"
STEPS: dict[str, Step] = {
    step.key: step
    for step in (
        Step("ootf-source", "Read the Drakan 1 files from your disc image", 0.5, 2.0, basis="InstallShield cabinet, 307 MB"),
        Step("ootf-data", "Read the level template from your Drakan 2 ISO", 0.1, 0.5, basis="127 MB read from the ISO"),
        Step("ootf-convert", "Convert the 23 levels (terrain, models, creatures, scenes, music)", 2.5, 5.0, basis="168 s measured"),
        Step("hd-tools", "Download the HD tools (about 75 MB, once)", 0.5, 3.0, once=True, basis="Real-ESRGAN, numpy, Pillow"),
        Step("hd-index", "Decode and index every texture", 0.5, 1.5, basis="15 s disc + 18 s Order of the Flame measured"),
        Step("hd-upscale", "Upscale the Drakan 2 textures x4 (2,962 textures, 1.2 GB)", 10.0, 15.0, gpu=True, basis="616 s on the reference GPU"),
        Step("hd-upscale-ootf", "Upscale the Order of the Flame textures (about 1,400 new ones)", 2.5, 4.0, gpu=True, basis="162 s on the reference GPU"),
    )
}  # fmt: skip
DISK_GB = {"ootf": 1.0, "hd": 2.0, "hd-ootf": 1.0}  # work files plus the finished pack; approximate


@dataclass(frozen=True)
class Gpu:
    name: str
    factor: float  # time multiplier against the reference GPU
    tier: str  # fast | good | modest | integrated | unknown | none
    usable: bool

    @property
    def description(self) -> str:
        return {
            "fast": "fast",
            "good": "good",
            "modest": "modest",
            "integrated": "integrated graphics, slow",
            "unknown": "unknown",
            "none": "no Vulkan GPU found",
        }[self.tier]


_GPU_TIERS = (
    ("none", 0.0, r"basic (display|render)|remote display|virtual|vmware|hyper-v|parallels|citrix|rdp"),
    ("fast", 1.0, r"rtx\s*(50|40)\d\d|radeon rx\s*(79|78|77|90)\d\d|arc\s*b\d{3}"),
    ("good", 2.0, r"rtx\s*(30|20)\d\d|radeon rx\s*(7|6)\d{3}|arc\s*a[5-9]\d\d|titan"),
    ("modest", 4.0, r"gtx\s*(16|10|9)\d\d|radeon rx\s*(5\d{3}|[45]\d\d)\b|vega\s*(56|64)|radeon vii|arc\s*a3\d\d"),
    ("integrated", 10.0, r"uhd|iris|hd graphics|radeon(\(tm\))? graphics|vega\s*\d+ graphics|adreno|intel"),
)


def classify_gpu(name: str | None) -> Gpu:
    """Rough class of a graphics adapter from its Windows name. Only the RTX 5080 is measured; the rest is a multiplier."""
    text = (name or "").strip()
    if not text:
        return Gpu("", 4.0, "unknown", True)
    lowered = text.lower()
    for tier, factor, pattern in _GPU_TIERS:
        if re.search(pattern, lowered):
            return Gpu(text, factor, tier, tier != "none")
    return Gpu(text, 4.0, "unknown", True)


def best_gpu(names: list[str]) -> Gpu:
    """The adapter the HD step should use: the fastest one that can run Vulkan."""
    candidates = [classify_gpu(name) for name in names if name and name.strip()] or [classify_gpu(None)]
    usable = [gpu for gpu in candidates if gpu.usable]
    return min(usable, key=lambda gpu: gpu.factor) if usable else candidates[0]


def plan(source_kind: str | None, hd: bool, tools_ready: bool = False, ootf_in_hd: bool | None = None) -> list[str]:
    """The step keys a run performs, in order. source_kind is None (no Order of the Flame), install, disc-folder or image."""
    keys: list[str] = []
    if source_kind:
        if source_kind != "install":
            keys.append("ootf-source")
        keys += ["ootf-data", "ootf-convert"]
    if hd:
        if not tools_ready:
            keys.append("hd-tools")
        keys += ["hd-index", "hd-upscale"]
        if source_kind if ootf_in_hd is None else ootf_in_hd:
            keys.append("hd-upscale-ootf")
    return keys


@dataclass(frozen=True)
class Line:
    key: str
    title: str
    low: float
    high: float
    measured: bool = False


@dataclass(frozen=True)
class Estimate:
    lines: list[Line]
    gpu: Gpu
    disk_gb: float

    @property
    def low(self) -> float:
        return sum(line.low for line in self.lines)

    @property
    def high(self) -> float:
        return sum(line.high for line in self.lines)


def estimate(keys: list[str], gpu: Gpu, measured: dict[str, float] | None = None, ootf: bool = False) -> Estimate:
    """Minutes per step. A step this PC has timed before (seconds in `measured`) uses that figure; otherwise the reference
    figures, scaled by the graphics card for the upscale steps."""
    measured = measured or {}
    lines = []
    for key in keys:
        step = STEPS[key]
        if key in measured:
            minutes = measured[key] / 60
            lines.append(Line(key, step.title, minutes, minutes, True))
            continue
        factor = gpu.factor if step.gpu else 1.0
        lines.append(Line(key, step.title, step.low * factor, step.high * factor))
    disk = (DISK_GB["ootf"] if ootf else 0) + (DISK_GB["hd"] if "hd-index" in keys else 0)
    disk += DISK_GB["hd-ootf"] if "hd-upscale-ootf" in keys else 0
    return Estimate(lines, gpu, disk)


def format_minutes(low: float, high: float | None = None) -> str:
    high = low if high is None else high

    def one(value: float) -> str:
        if value < 1:
            return "under a minute"
        if value < 90:
            return f"{int(value + 0.5)} min"
        hours = value / 60
        return f"{hours:.1f} h" if hours < 10 else f"{round(hours)} h"

    if high < 1:
        return "under a minute"
    first, second = one(low), one(high)
    if first == second or high - low < 1:
        return f"about {second}"
    if low < 1:
        return f"up to {second}"
    # "10 to 15 min": drop the unit from the first number when both share it
    unit = second.split(" ", 1)[1]
    if first.endswith(unit):
        first = first[: -len(unit) - 1]
    return f"{first} to {second}"


def format_estimate(est: Estimate) -> str:
    out = []
    width = max((len(line.title) for line in est.lines), default=0)
    for line in est.lines:
        note = " (this PC, last run)" if line.measured else ""
        out.append(f"  {line.title.ljust(width)}  {format_minutes(line.low, line.high)}{note}")
    if est.lines:
        out.append(f"  {'Total'.ljust(width)}  {format_minutes(est.low, est.high)}")
        if est.disk_gb:
            out.append(f"  {'Disk space'.ljust(width)}  about {est.disk_gb:.0f} GB")
    return "\n".join(out)


def reference_summary() -> dict[str, tuple[str, float]]:
    """Time (as shown to the player) and disk for the three choices on a fast graphics card, with the tools not yet
    downloaded: the text on DrakanPC-Setup.iss's extras page is checked against this (scripts/tests/test_builder_extras.py)."""
    gpu = classify_gpu("NVIDIA GeForce RTX 4070")
    out = {}
    for name, (kind, hd) in {"ootf": ("image", False), "hd": (None, True), "both": ("image", True)}.items():
        est = estimate(plan(kind, hd, False), gpu, None, ootf=bool(kind))
        out[name] = (format_minutes(est.low, est.high), est.disk_gb)
    return out


def gpu_notes(gpu: Gpu) -> list[str]:
    notes = []
    if gpu.tier == "none":
        notes.append("No graphics card that can run Vulkan was found, so the upscaler cannot run in a sensible time.")
    elif gpu.tier == "integrated":
        notes.append(
            f"{gpu.name or 'The graphics'} is integrated graphics: the upscale works but takes roughly {gpu.factor:.0f} times "
            f"as long as on the reference {REFERENCE_GPU}."
        )
    elif gpu.tier == "unknown":
        notes.append("Your graphics card is not in the timing table; the upscale estimate is a rough guess.")
    if gpu.tier != "none":
        notes.append(f"Only the {REFERENCE_GPU} was timed; other cards are scaled from that.")
    return notes


# --------------------------------------------------------------------------------------------------------- downloads
def sha256_of(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def fetch_with_system_tools(url: str, destination: Path) -> bool:
    """curl.exe (Windows 10 and later) or PowerShell, which use Windows' own certificate handling. Python's TLS stack
    can fail on a server chain that Windows completes itself ("unable to get issuer certificate"). The caller still
    checks the SHA-256, so the transport is not trusted for content."""
    attempts = []
    curl = shutil.which("curl.exe") or shutil.which("curl")
    if curl:
        attempts.append([curl, "-L", "--fail", "--silent", "--show-error", "--output", str(destination), url])
    shell = shutil.which("pwsh") or shutil.which("powershell")
    if shell:
        command = (
            f"$ProgressPreference='Continue'; Invoke-WebRequest -UseBasicParsing -Uri '{url}' -OutFile '{destination}'"
        )
        attempts.append([shell, "-NoProfile", "-Command", command])
    for command in attempts:
        if subprocess.run(command, check=False).returncode == 0 and destination.is_file():
            return True
        destination.unlink(missing_ok=True)
    return False


def download(spec: dict, cache: Path, opener=None) -> Path:
    """Fetch a pinned file into the cache once; refuse (and delete) anything whose SHA-256 differs from the lock.
    With the default opener a TLS failure falls back to curl / PowerShell; an `opener` given by a caller (tests) does not."""
    system_fallback = opener is None
    opener = opener or urllib.request.urlopen
    cache.mkdir(parents=True, exist_ok=True)
    target = cache / spec["url"].rsplit("/", 1)[1]
    if target.is_file() and sha256_of(target) != spec["sha256"]:
        target.unlink()
    if not target.is_file():
        print(f"Downloading {spec['url']}", flush=True)
        partial = target.with_suffix(target.suffix + ".part")
        try:
            with opener(spec["url"]) as response, open(partial, "wb") as stream:
                shutil.copyfileobj(response, stream)
        except OSError as error:  # URLError and ssl.SSLError are OSErrors
            partial.unlink(missing_ok=True)
            if not system_fallback or not fetch_with_system_tools(spec["url"], partial):
                raise ExtrasError(f"Could not download {spec['url']}: {error}") from error
        partial.replace(target)
    if sha256_of(target) != spec["sha256"]:
        target.unlink()
        raise ExtrasError(f"{target.name} does not match the SHA-256 pinned in installer.lock.json; it was deleted.")
    return target


def safe_extract_zip(archive: Path, target: Path, strip: int = 0) -> None:
    """Unpack a zip, refusing absolute and parent-relative names. `strip` drops leading folders."""
    root = target.resolve()
    with zipfile.ZipFile(archive) as z:
        for info in z.infolist():
            parts = PurePosixPath(info.filename).parts[strip:]
            if info.is_dir() or not parts:
                continue
            if any(part in ("", "..") or ":" in part or part.startswith("\\") for part in parts):
                raise ExtrasError(f"{archive.name}: unsafe path {info.filename!r}")
            destination = target.joinpath(*parts)
            if not destination.resolve().is_relative_to(root):
                raise ExtrasError(f"{archive.name}: {info.filename!r} escapes the target folder")
            destination.parent.mkdir(parents=True, exist_ok=True)
            with z.open(info) as source, open(destination, "wb") as out:
                shutil.copyfileobj(source, out)


def safe_extract_tgz(archive: Path, target: Path, strip: int = 1) -> None:
    root = target.resolve()
    with tarfile.open(archive) as tar:
        for member in tar.getmembers():
            parts = PurePosixPath(member.name).parts[strip:]
            if not member.isfile() or not parts:
                if not member.isdir() and not member.isfile():
                    raise ExtrasError(f"{archive.name}: links and special files are not unpacked ({member.name})")
                continue
            if any(part in ("", "..") or ":" in part for part in parts):
                raise ExtrasError(f"{archive.name}: unsafe path {member.name!r}")
            destination = target.joinpath(*parts)
            if not destination.resolve().is_relative_to(root):
                raise ExtrasError(f"{archive.name}: {member.name!r} escapes the target folder")
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(tar.extractfile(member).read())


def load_lock(path: Path = LOCK) -> dict:
    return json.loads(path.read_text(encoding="utf-8"))


# --------------------------------------------------------------------------------------------------------- Drakan 1 source
def child(folder: Path, name: str) -> Path | None:
    """A direct child of `folder` named `name`, whatever its letter case (disc folders are often upper case)."""
    try:
        for entry in folder.iterdir():
            if entry.name.lower() == name.lower():
                return entry
    except OSError:
        pass
    return None


def is_install(folder: Path) -> bool:
    """A Drakan: Order of the Flame data folder: Dragon.rrc next to a world folder."""
    rrc = child(folder, "Dragon.rrc")
    return bool(
        rrc
        and rrc.is_file()
        and any((w := child(folder, n)) and w.is_dir() for n in ("Mountain World", "Islands World"))
    )


def find_install(folder: Path) -> Path | None:
    """The install folder itself, or one inside it (a picked parent such as C:\\Games\\Drakan holding Drakan\\)."""
    if is_install(folder):
        return folder
    try:
        for entry in sorted(folder.iterdir()):
            if entry.is_dir() and is_install(entry):
                return entry
    except OSError:
        pass
    return None


def sniff_iso(path: Path) -> bool:
    """True for a raw 2048-byte-sector ISO9660 volume starting at 0 (.iso) or 0x40 (the retail .mdx)."""
    try:
        with open(path, "rb") as stream:
            for base in (0, 0x40):
                stream.seek(base + 16 * 2048)
                if stream.read(8)[1:6] == b"CD001":
                    return True
    except OSError:
        pass
    return False


SOURCE_HELP = (
    "Choose the folder Drakan: Order of the Flame is installed in (it holds Dragon.rrc and the 'Mountain World' folder), "
    "a copy of its disc (SETUP/DATA1.CAB), its disc image (.mdx, .iso, or a .cue with its .bin tracks), or a .zip that "
    "holds one of those."
)


def classify_source(path: str | Path) -> tuple[str, Path]:
    """What a Drakan 1 source is: ('install', folder), ('disc-folder', DATA1.CAB) or ('image', file)."""
    source = Path(path)
    if source.is_dir():
        install = find_install(source)
        if install:
            return "install", install
        cab = ootf_cab().find_cab_file(source)
        if cab:
            return "disc-folder", cab
        raise ExtrasError(f"{source} is not a Drakan: Order of the Flame install or disc. {SOURCE_HELP}")
    if not source.is_file():
        raise ExtrasError(f"Drakan 1 source not found: {source}")
    suffix = source.suffix.lower()
    if suffix == ".zip":
        if not zipfile.is_zipfile(source):
            raise ExtrasError(f"{source} is not a readable zip file.")
        with zipfile.ZipFile(source) as z:
            names = z.namelist()
        images = [n for n in names if n.lower().endswith((".mdx", ".iso"))]
        cues = [n for n in names if n.lower().endswith(".cue")]
        if len(images) == 1 or (not images and len(cues) == 1):
            return "image", source
        raise ExtrasError(
            f"{source.name} must hold one .mdx or .iso disc image, or one .cue sheet with its .bin tracks; "
            f"it holds {len(images)} images and {len(cues)} cue sheets."
        )
    if suffix in (".mdx", ".iso"):
        if not sniff_iso(source):
            raise ExtrasError(f"{source.name} is not a raw ISO9660 disc image.")
        return "image", source
    if suffix == ".cue":
        track = ootf_cab().cue_data_track(source.read_text(encoding="latin-1", errors="replace"))
        if not track:
            raise ExtrasError(f"{source.name} lists no data track.")
        if not (source.parent / track[0]).is_file():
            raise ExtrasError(f"{source.name} names {track[0]}, which is not next to it.")
        return "image", source
    if suffix == ".bin":
        with open(source, "rb") as stream:
            head = stream.read(18 * 2352)
        if ootf_cab().sniff_skip(head) is None:
            raise ExtrasError(f"{source.name} holds no data track (an audio track?). Choose the .cue sheet or Track 1.")
        return "image", source
    raise ExtrasError(f"Unsupported Drakan 1 source {source.name}. {SOURCE_HELP}")


def verify_source(path: str | Path) -> str:
    """Everything short of unpacking: the source is readable, and a disc's cabinet lists the Order of the Flame files.
    Returns a one-line description or raises ExtrasError. The installer and Build-Drakan.ps1 run this before any long step,
    so a wrong choice is reported at once instead of stopping the build."""
    kind, where = classify_source(path)
    if kind == "install":
        return f"install folder {where}"
    cab = ootf_cab()
    try:
        if kind == "disc-folder":
            data = where.read_bytes()
        else:
            image = cab.read_image(where)
            base = 0 if image[16 * 2048 + 1 : 16 * 2048 + 6] == b"CD001" else 0x40
            offset, size = cab.iso_find(image, base, "SETUP/DATA1.CAB")
            data = image[offset : offset + size]
        names = {name.lower() for _, name, *_ in cab.read_cab(data)}
    except FileNotFoundError:
        raise ExtrasError(
            f"{where.name} is a disc image but has no SETUP/DATA1.CAB: it is not the Order of the Flame PC disc."
        ) from None
    except (ValueError, OSError, zipfile.BadZipFile) as error:
        raise ExtrasError(f"{where.name} could not be read as the Order of the Flame disc: {error}") from error
    if "dragon.rrc" not in names:
        raise ExtrasError(f"{where.name} is a disc but not Drakan: Order of the Flame (its cabinet has no Dragon.rrc).")
    return f"{kind} {where} ({len(names)} files in the cabinet)"


def ootf_cab():
    """analysis/scripts/ootf_cab.py (the InstallShield cabinet reader), loaded by path."""
    spec = importlib.util.spec_from_file_location("ootf_cab", ANALYSIS / "ootf_cab.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


COVER = "scripts/buildtool/kit/art/ootf-cover.png"


def bundled_cover(root: Path = ROOT) -> Path | None:
    """The Order of the Flame campaign cover tracked in the repository (the pack's card picture and title background),
    or None when this tree lacks it. The converter ships it as previews/campaign-ootf.png and background-ootf.png."""
    path = root / COVER
    return path if path.is_file() else None


def ps2_data():
    spec = importlib.util.spec_from_file_location("ootf_ps2_data", ANALYSIS / "ootf_ps2_data.py")
    sys.path.insert(0, str(ANALYSIS))
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def file_stamp(path: Path) -> list[int]:
    info = path.stat()
    return [info.st_size, int(info.st_mtime)]


def prepare_source(source: str | Path, work: Path, log=print) -> Path:
    """The folder the converter reads as --ootf. An install is used where it is; a disc image or cabinet is unpacked."""
    kind, where = classify_source(source)
    if kind == "install":
        return where
    out = work / "cab_disc"
    stamp = work / "cab_disc.source"
    mark = json.dumps([str(where), file_stamp(where)])
    if is_install(out) and stamp.is_file() and stamp.read_text(encoding="utf-8") == mark:
        log(f"Drakan 1 files already unpacked: {out}")
        return out
    log(f"Unpacking the Drakan 1 files from {where.name} (about 300 MB)")
    if out.exists():
        shutil.rmtree(out)
    cab = ootf_cab()
    count = cab.extract_cab(where.read_bytes(), out) if kind == "disc-folder" else cab.extract_image(where, out)
    if not is_install(out):
        raise ExtrasError(
            f"{where.name} unpacked {count} files but they are not Drakan: Order of the Flame. {SOURCE_HELP}"
        )
    stamp.write_text(mark, encoding="utf-8")
    return out


# --------------------------------------------------------------------------------------------------------- node modules
NODE_WORKSPACES = {
    "@drakan/formats": "packages/formats/src/index.ts",
    "@drakan/editor-core": "packages/editor-core/src/index.ts",
}


def ensure_node_modules(root: Path, lock: dict, cache: Path, fetch=download) -> list[str]:
    """Make the converter's imports resolve without npm: pinned fflate (the only third-party package it needs) and one
    re-exporting stub per editor workspace package. A normal `npm install` (a developer checkout) is left alone."""
    editor = root / "editor"
    modules = editor / "node_modules"
    made = []
    for name, entry in NODE_WORKSPACES.items():
        if (modules / name / "package.json").is_file():
            continue
        if not (editor / entry).is_file():
            raise ExtrasError(
                f"{entry} is missing: the editor sources (the formats submodule) are not in this checkout."
            )
        stub = modules / name
        stub.mkdir(parents=True, exist_ok=True)
        (stub / "package.json").write_text(
            json.dumps(
                {"name": name, "private": True, "type": "module", "main": "index.js", "exports": {".": "./index.js"}}
            ),
            encoding="utf-8",
        )
        # index.js re-exports the TypeScript entry; Node cannot strip types inside node_modules, but the entry lives outside it.
        depth = "../" * (len(PurePosixPath(name).parts) + 1)
        (stub / "index.js").write_text(f"export * from '{depth}{entry}';\n", encoding="utf-8")
        made.append(name)
    if not (modules / "fflate" / "package.json").is_file():
        archive = fetch(lock["hd"]["fflate"], cache)
        safe_extract_tgz(archive, modules / "fflate")
        made.append("fflate")
    return made


# --------------------------------------------------------------------------------------------------------- converter
def code_version(root: Path = ROOT) -> str:
    """Identifies the converter code: the kit's KIT-VERSION.txt, else the Git revision, else 'dev'."""
    marker = root / "KIT-VERSION.txt"
    if marker.is_file():
        return marker.read_text(encoding="utf-8").strip()
    try:
        done = subprocess.run(
            ["git", "-C", str(root), "rev-parse", "HEAD"], capture_output=True, text=True, timeout=20, check=False
        )
        sha = done.stdout.strip()
        if done.returncode == 0 and re.fullmatch(r"[0-9a-f]{40}", sha):
            # the converter sources live partly in a submodule; its revision counts too
            sub = subprocess.run(
                ["git", "-C", str(root / "editor/packages/formats"), "rev-parse", "HEAD"],
                capture_output=True, text=True, timeout=20, check=False,
            )  # fmt: skip
            return sha + (":" + sub.stdout.strip() if sub.returncode == 0 else "")
    except (OSError, subprocess.SubprocessError):
        pass
    return "dev"


def convert_state(install: Path, iso: Path, hd: bool, version: str, cover: Path | None = None) -> dict:
    rrc = child(install, "Dragon.rrc")
    return {"install": str(install), "rrc": file_stamp(rrc) if rrc else None, "iso": [str(iso), file_stamp(iso)],
            "hd": hd, "code": version,
            "cover": hashlib.sha1(cover.read_bytes()).hexdigest() if cover and cover.is_file() else None}  # fmt: skip


def convert_needed(work: Path, state: dict, force: bool = False) -> bool:
    zip_path = work / "mods" / f"{PACK_ID}.zip"
    saved = work / "convert-state.json"
    if force or not zip_path.is_file() or not saved.is_file():
        return True
    try:
        previous = json.loads(saved.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return True
    if state["hd"] and not previous.get("hd"):
        return True  # lossless HD sources were not written last time
    return {k: v for k, v in previous.items() if k != "hd"} != {k: v for k, v in state.items() if k != "hd"}


def converter_command(
    node: str, install: Path, data: Path, iso: Path, out: Path, hd: bool, cover: Path | None = None
) -> list[str]:
    cmd = [
        node, "--experimental-transform-types", "--no-warnings", CONVERTER,
        "--ootf", str(install), "--ps2", str(data), "--template", TEMPLATE,
        "--campaign", "1", "--id", PACK_ID, "--iso", str(iso), "--out", str(out),
    ]  # fmt: skip
    if cover:
        # the campaign's card cover and title / loading background (previews/campaign-ootf.png, background-ootf.png)
        cmd += ["--cover", str(cover)]
    if hd:
        # lossless pixels of the textures the converter has to quantise, next to the pack (<out>.hdsrc); the HD step
        # indexes and upscales them itself, so the converter does not start its own HD run
        cmd += ["--hd", "--hd-sources-only", "1"]
    return cmd


def run_logged(cmd: list[str], log_file: Path, cwd: Path, env: dict | None = None) -> int:
    log_file.parent.mkdir(parents=True, exist_ok=True)
    with open(log_file, "w", encoding="utf-8", errors="replace") as log:
        return subprocess.run(cmd, cwd=cwd, stdout=log, stderr=subprocess.STDOUT, env=env, check=False).returncode


def tail(path: Path, lines: int = 12) -> str:
    try:
        return "\n".join(path.read_text(encoding="utf-8", errors="replace").splitlines()[-lines:])
    except OSError:
        return ""


def write_mods_ini(directory: Path) -> None:
    (directory / "mods.ini").write_text(f"[mods]\n{PACK_ID} = on\n", encoding="utf-8", newline="\n")


def convert_ootf(
    source: str | Path, iso: Path, work: Path, node: str, hd: bool = False, force: bool = False, root: Path = ROOT
) -> Path:
    """Source -> <work>/mods/ootf-levels.zip (+ mods.ini). Returns the mods folder to pass as Build-LocalGame -ModDirectory."""
    timings = Timings(work.parent / "extras-timings.json")
    started = time.time()
    kind, _ = classify_source(source)
    install = prepare_source(source, work)
    if kind != "install":
        timings.record("ootf-source", time.time() - started)
    if not iso.is_file():
        raise ExtrasError(f"ISO not found: {iso}")
    mods = work / "mods"
    cover = bundled_cover(root)
    state = convert_state(install, iso, hd, code_version(root), cover)
    if not convert_needed(work, state, force):
        print(f"Order of the Flame pack is up to date: {mods / (PACK_ID + '.zip')}")
        write_mods_ini(mods)
        return mods
    mark = time.time()
    data = work / "ps2" / "data"
    module = ps2_data()
    if not module.is_complete(data, TEMPLATE):
        print("Reading the level template from your Drakan 2 ISO", flush=True)
        module.extract(iso, data, TEMPLATE)
        if not module.is_complete(data, TEMPLATE):
            raise ExtrasError("The ISO lacks the level template files; is it the Drakan: The Ancients' Gates disc?")
    timings.record("ootf-data", time.time() - mark)
    ensure_node_modules(
        root, load_lock(root / "scripts/buildtool/kit/installer.lock.json"), work.parent / "extras-cache"
    )
    mods.mkdir(parents=True, exist_ok=True)
    out = mods / f"{PACK_ID}.zip"
    out.unlink(missing_ok=True)
    if hd:
        shutil.rmtree(out.with_name(out.name + ".hdsrc"), ignore_errors=True)
    log_file = work / "convert.log"
    step = STEPS["ootf-convert"]
    print(
        f"Converting the Order of the Flame levels ({format_minutes(step.low, step.high)}); log: {log_file}", flush=True
    )
    mark = time.time()
    environment = dict(os.environ, DRAKAN_ISO=str(iso))
    code = run_logged(converter_command(node, install, data, iso, out, hd, cover), log_file, root, environment)
    if code or not out.is_file():
        raise ExtrasError(f"The converter stopped (exit code {code}). Last lines of {log_file}:\n{tail(log_file)}")
    timings.record("ootf-convert", time.time() - mark)
    write_mods_ini(mods)
    (work / "convert-state.json").write_text(json.dumps(state), encoding="utf-8")
    wrote = next(
        (
            line
            for line in reversed(log_file.read_text(encoding="utf-8", errors="replace").splitlines())
            if line.startswith("wrote ")
        ),
        "",
    )
    print(wrote or f"wrote {out}")
    return mods


# --------------------------------------------------------------------------------------------------------- HD textures
@dataclass(frozen=True)
class HdTools:
    exe: Path
    site: Path | None  # extra sys.path for numpy / Pillow, None when the interpreter has them


def python_has_modules(python: str, site: Path | None = None) -> bool:
    code = "import sys\nif len(sys.argv) > 1: sys.path.insert(0, sys.argv[1])\nimport numpy, PIL"
    cmd = [python, "-I", "-c", code] + ([str(site)] if site else [])
    return subprocess.run(cmd, capture_output=True, check=False).returncode == 0


def realesrgan_ready(folder: Path) -> bool:
    return (folder / "realesrgan-ncnn-vulkan.exe").is_file() and (folder / "models" / "realesrgan-x4plus.bin").is_file()


def hd_tools_ready(root: Path = ROOT, python: str | None = None) -> bool:
    python = python or sys.executable
    return realesrgan_ready(root / "tools/hdpack/realesrgan") and (
        python_has_modules(python) or python_has_modules(python, root / "tools/hd-site")
    )


def provision_hd_tools(root: Path, lock: dict, cache: Path, python: str | None = None, fetch=download) -> HdTools:
    """Real-ESRGAN, numpy and Pillow, each pinned by SHA-256. A Python that already has numpy and Pillow is used as it is."""
    python = python or sys.executable
    folder = root / "tools/hdpack/realesrgan"
    if not realesrgan_ready(folder):
        archive = fetch(lock["hd"]["realesrgan"], cache)
        shutil.rmtree(folder, ignore_errors=True)
        safe_extract_zip(archive, folder)
        if not realesrgan_ready(folder):
            raise ExtrasError("The Real-ESRGAN download did not contain the upscaler and its models.")
    exe = folder / "realesrgan-ncnn-vulkan.exe"
    if python_has_modules(python):
        return HdTools(exe, None)
    site = root / "tools/hd-site"
    if python_has_modules(python, site):
        return HdTools(exe, site)
    version = subprocess.run(
        [python, "-I", "-c", "import sys;print(sys.version_info[:2] == (3, 12))"],
        capture_output=True,
        text=True,
        check=False,
    )
    if version.stdout.strip() != "True":
        raise ExtrasError(
            "The HD step needs numpy and Pillow. This Python is not 3.12, which the pinned wheels are built for: "
            "run 'python -m pip install numpy pillow' for it, or use the builder's own Python."
        )
    shutil.rmtree(site, ignore_errors=True)
    for name in ("numpy", "pillow"):
        safe_extract_zip(fetch(lock["hd"][name], cache), site)
    if not python_has_modules(python, site):
        raise ExtrasError("numpy and Pillow were unpacked but this Python cannot import them.")
    return HdTools(exe, site)


def hd_command(python: str, script: Path, sub: str, *args: str) -> list[str]:
    return [python, "-I", str(script), sub, *args]


def run_hd(
    iso: Path, play: Path, work: Path, packs: list[Path], python: str | None = None, gpu_index: int | None = None,
    root: Path = ROOT, fetch=download, run=subprocess.run,
) -> Path:  # fmt: skip
    """Index the disc's and the packs' textures, then upscale what the play folder's textures folder does not have yet."""
    python = python or sys.executable
    timings = Timings(work.parent / "extras-timings.json")
    textures = play / "textures"
    mark = time.time()
    tools = provision_hd_tools(
        root, load_lock(root / "scripts/buildtool/kit/installer.lock.json"), work.parent / "extras-cache", python, fetch
    )
    timings.record("hd-tools", time.time() - mark)
    env = dict(os.environ)
    if tools.site:
        env["DRAKAN_PY_SITE"] = str(tools.site)
    script = ANALYSIS / "hd_pack.py"

    def hd_run(sub: str, *args: str) -> int:
        return run(hd_command(python, script, sub, *args), env=env, check=False).returncode

    def upscale(label: str) -> None:
        options = ["--work", str(work), "--out", str(textures), "--exe", str(tools.exe)]
        if gpu_index is not None:
            options += ["--gpu", str(gpu_index)]
        print(f"Upscaling {label} with Real-ESRGAN; this is the long step and can be stopped and resumed", flush=True)
        if hd_run("upscale", *options):
            raise ExtrasError(
                "Some textures could not be upscaled (see above). Run the build again to retry them; "
                "check that a Vulkan graphics driver is installed."
            )

    mark = time.time()
    if not (work / "index.json").is_file() or not (work / "src").is_dir():
        print("Decoding every Drakan 2 texture (about 15 s)", flush=True)
        if hd_run("extract", "--iso", str(iso), "--work", str(work)):
            raise ExtrasError("Decoding the disc's textures failed.")
    timings.record("hd-index", time.time() - mark)
    mark = time.time()
    upscale("the Drakan 2 textures")
    timings.record("hd-upscale", time.time() - mark)
    if packs:
        mark = time.time()
        for pack in packs:
            print(f"Indexing the textures of {pack.name}", flush=True)
            if hd_run("modextract", "--zip", str(pack), "--work", str(work)):
                raise ExtrasError(f"Indexing the textures of {pack.name} failed.")
        timings.record("hd-index", timings.load().get("hd-index", 0) + time.time() - mark)
        mark = time.time()
        upscale("the Order of the Flame textures")
        timings.record("hd-upscale-ootf", time.time() - mark)
    return textures


# --------------------------------------------------------------------------------------------------------- timings
class Timings:
    """Seconds each step took the last time on this PC (build/extras-timings.json), used for later estimates."""

    def __init__(self, path: Path):
        self.path = path

    def load(self) -> dict[str, float]:
        try:
            data = json.loads(self.path.read_text(encoding="utf-8"))
            return {k: float(v) for k, v in data.items() if k in STEPS}
        except (OSError, ValueError, AttributeError):
            return {}

    def record(self, key: str, seconds: float) -> None:
        data = self.load()
        data[key] = round(seconds, 1)
        self.path.parent.mkdir(parents=True, exist_ok=True)
        self.path.write_text(json.dumps(data, indent=2), encoding="utf-8")


# --------------------------------------------------------------------------------------------------------- command line
def cmd_estimate(args) -> int:
    kind = None
    if args.ootf:
        kind = "image"
        if args.source:
            try:
                kind = classify_source(args.source)[0]
            except ExtrasError as error:
                print(f"note: {error}", file=sys.stderr)
    gpu = classify_gpu(args.gpu) if not args.gpu_names else best_gpu(args.gpu_names.split("|"))
    tools_ready = args.tools_ready or (args.hd and hd_tools_ready())
    keys = plan(kind or ("image" if args.ootf else None), args.hd, tools_ready)
    measured = Timings(Path(args.timings)).load() if args.timings else {}
    result = estimate(keys, gpu, measured, ootf=bool(args.ootf))
    if args.json:
        print(json.dumps({"steps": [line.__dict__ for line in result.lines], "low": result.low, "high": result.high,
                          "diskGb": result.disk_gb, "gpu": gpu.__dict__}))  # fmt: skip
        return 0
    if not result.lines:
        print("Nothing selected.")
        return 0
    print(f"Estimated time on this PC (graphics: {gpu.name or 'not detected'}, {gpu.description})")
    print(format_estimate(result))
    for note in gpu_notes(gpu) if args.hd else []:
        print(f"  Note: {note}")
    return 0


def cmd_check_source(args) -> int:
    try:
        if args.deep:
            print(verify_source(args.source))
            return 0
        kind, where = classify_source(args.source)
    except ExtrasError as error:
        print(error, file=sys.stderr)
        return 1
    print(f"{kind}: {where}")
    return 0


def cmd_ootf(args) -> int:
    try:
        mods = convert_ootf(
            args.source,
            Path(args.iso),
            Path(args.work),
            args.node or shutil.which("node") or "node",
            args.hd,
            args.force,
        )
    except ExtrasError as error:
        print(f"Order of the Flame was not converted: {error}", file=sys.stderr)
        return 1
    print(f"MODS={mods}")
    return 0


def cmd_hd(args) -> int:
    packs = [Path(p) for p in args.ootf_pack]
    missing = [p for p in packs if not p.is_file()]
    if missing:
        print(f"Pack not found: {missing[0]}", file=sys.stderr)
        return 1
    try:
        textures = run_hd(Path(args.iso), Path(args.play), Path(args.work), packs, gpu_index=args.gpu_index)
    except ExtrasError as error:
        print(f"HD textures were not finished: {error}", file=sys.stderr)
        return 1
    count = len(list(textures.glob("*.png")))
    print(f"HD textures: {count} files in {textures}")
    return 0


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    p = sub.add_parser("estimate", help="time and disk an extra costs")
    p.add_argument("--ootf", action="store_true")
    p.add_argument("--source", help="the Drakan 1 source, to know whether it must be unpacked")
    p.add_argument("--hd", action="store_true")
    p.add_argument("--gpu", help="graphics adapter name, as Windows reports it")
    p.add_argument("--gpu-names", help="all adapter names separated by '|'; the best usable one counts")
    p.add_argument("--tools-ready", action="store_true", help="the HD tools are already downloaded")
    p.add_argument("--timings", help="build/extras-timings.json from an earlier run")
    p.add_argument("--json", action="store_true")
    p.set_defaults(func=cmd_estimate)
    p = sub.add_parser("check-source", help="validate a Drakan 1 install, disc folder or image")
    p.add_argument("source")
    p.add_argument("--deep", action="store_true", help="also read the disc image and check its cabinet (a few seconds)")
    p.set_defaults(func=cmd_check_source)
    p = sub.add_parser("ootf", help="convert Drakan: Order of the Flame into a mod pack")
    p.add_argument("--iso", required=True, help="your Drakan 2 ISO (the level template comes from it)")
    p.add_argument("--source", required=True)
    p.add_argument("--work", default=str(ROOT / "build/ootf"))
    p.add_argument("--node")
    p.add_argument("--hd", action="store_true", help="keep lossless texture sources for the HD step")
    p.add_argument("--force", action="store_true", help="convert again even when nothing changed")
    p.set_defaults(func=cmd_ootf)
    p = sub.add_parser("hd", help="build the HD texture pack")
    p.add_argument("--iso", required=True)
    p.add_argument("--play", required=True, help="the play folder; textures go to <play>/textures")
    p.add_argument("--work", default=str(ROOT / "build/hdpack"))
    p.add_argument("--ootf-pack", action="append", default=[], help="converted pack whose textures are included")
    p.add_argument("--gpu-index", type=int, help="Vulkan device number for the upscaler (default: the first)")
    p.set_defaults(func=cmd_hd)
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
