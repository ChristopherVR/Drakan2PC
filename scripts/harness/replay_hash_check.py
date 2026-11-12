"""Frame hashes of GS captures: against a reference set, or between two configurations of one build.

    # a build against reference hashes made by an earlier build (the "nothing changed" check)
    python scripts/harness/replay_hash_check.py --bin <dir with gsr_replay.exe> --caps <dir> --out <dir> \
        --reference <dir with <capture>_<backend>_s<scale>_<hd>.txt>
    # two configurations of the same build against each other (e.g. records evaluated on the CPU and drawn by the
    # vertex shader)
    python scripts/harness/replay_hash_check.py --bin <dir> --caps <dir> --out <dir> \
        --variant cpu:DRAKAN_VERTEX_EVAL=records --variant gpu:DRAKAN_VERTEX_EVAL=gpu

`--caps` holds one folder per capture with a `gscap_*.bin` inside (a `PS2X_GS_CAPTURE` of the game). Every capture is
replayed with `gsr_replay` (Direct3D 11) or `gsr_replay_webgpu --gpu-api vulkan|d3d12` at every `--scale`, with
(`on`) and without (`off`) the HD texture pack of `--texdir`, and one hash per displayed frame is written to
`<out>/<variant>/<capture>_<backend>_s<scale>_<hd>.txt`. Two hash files are equal when every line is.

At most `--jobs` replays run at a time (4 is the agreed limit on the shared machine), and a replay only starts while
the GPU's dedicated memory in use is below `--gpu-mem-mib` (nvidia-smi; no check when it is missing). Captures are
game data and never part of the repository, so this is a tool for a local proof, not a unit test. Exit code 0: every
compared pair equal; 1: a difference or a failed replay; 2: nothing to compare.
"""

from __future__ import annotations

import argparse
import os
import subprocess
import sys
import time
from pathlib import Path


def gpu_memory_mib() -> int | None:
    try:
        out = subprocess.run(
            ["nvidia-smi", "--query-gpu=memory.used", "--format=csv,noheader,nounits"],
            capture_output=True,
            text=True,
            timeout=20,
            check=False,
        ).stdout
        return int(out.splitlines()[0].strip())
    except (OSError, ValueError, IndexError, subprocess.TimeoutExpired):
        return None


def parse_variant(text: str) -> tuple[str, dict[str, str]]:
    name, _, rest = text.partition(":")
    env: dict[str, str] = {}
    for item in filter(None, rest.split(",")):
        key, sep, value = item.partition("=")
        if not sep:
            raise SystemExit(f"--variant {text}: {item} is not KEY=VALUE")
        env[key] = value
    return name, env


def job_name(capture: str, backend: str, scale: int, hd: str) -> str:
    return f"{capture}_{backend}_s{scale}_{hd}"


def compare(a: Path, b: Path) -> tuple[int, int]:
    """Frames compared and frames that differ (a missing or shorter file counts every missing frame)."""
    la = a.read_text().splitlines() if a.is_file() else []
    lb = b.read_text().splitlines() if b.is_file() else []
    frames = max(len(la), len(lb))
    differ = sum(1 for i in range(frames) if i >= len(la) or i >= len(lb) or la[i] != lb[i])
    return frames, differ


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bin", required=True, type=Path, help="folder with gsr_replay(.exe) and gsr_replay_webgpu(.exe)")
    ap.add_argument("--caps", required=True, type=Path)
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument("--reference", type=Path, help="folder of reference hash files (compared with every variant)")
    ap.add_argument("--variant", action="append", default=[], help="NAME or NAME:KEY=VALUE[,KEY=VALUE...]; repeatable")
    ap.add_argument("--captures", default="", help="comma list of capture folder names (default: all)")
    ap.add_argument("--backends", default="d3d11,vulkan,d3d12")
    ap.add_argument("--scales", default="1,3,8")
    ap.add_argument("--hd", default="on", help="on, off or on,off")
    ap.add_argument("--texdir", type=Path, help="HD texture pack for hd = on")
    ap.add_argument("--frames", type=int, default=0, help="stop after this many frames (0 = the whole capture)")
    ap.add_argument("--jobs", type=int, default=4)
    ap.add_argument("--gpu-mem-mib", type=int, default=12000)
    ap.add_argument("--reuse", action="store_true", help="keep hash files that already exist in --out")
    args = ap.parse_args()

    variants = [parse_variant(v) for v in args.variant] or [("run", {})]
    wanted = [c for c in args.captures.split(",") if c]
    captures: list[tuple[str, Path]] = []
    for folder in sorted(p for p in args.caps.iterdir() if p.is_dir()):
        files = sorted(folder.glob("gscap_*.bin"))
        if files and (not wanted or folder.name in wanted):
            captures.append((folder.name, files[0]))
    if not captures:
        print("no captures found")
        return 2
    hd_modes = [h for h in args.hd.split(",") if h]
    if "on" in hd_modes and not args.texdir:
        raise SystemExit("--hd on needs --texdir")
    suffix = ".exe" if os.name == "nt" else ""

    jobs = []
    for vname, venv in variants:
        for cname, cfile in captures:
            for backend in args.backends.split(","):
                for scale in (int(s) for s in args.scales.split(",")):
                    for hd in hd_modes:
                        out = args.out / vname / (job_name(cname, backend, scale, hd) + ".txt")
                        if args.reuse and out.is_file() and out.stat().st_size:
                            continue
                        exe = args.bin / (("gsr_replay" if backend == "d3d11" else "gsr_replay_webgpu") + suffix)
                        cmd = [str(exe), str(cfile), "--scale", str(scale), "--no-png", "--hashes", str(out)]
                        cmd += ["--bench", "0-999999"]
                        if args.frames:
                            cmd += ["--max", str(args.frames)]
                        if backend != "d3d11":
                            cmd += ["--gpu-api", backend]
                        if hd == "on":
                            cmd += ["--texdir", str(args.texdir)]
                        jobs.append((out, cmd, venv))

    running: list[tuple[subprocess.Popen, Path]] = []
    failed: list[str] = []

    def reap(block: bool) -> None:
        while running:
            for item in list(running):
                proc, out = item
                if proc.poll() is not None:
                    running.remove(item)
                    if proc.returncode != 0:
                        failed.append(f"{out}: replay exit code {proc.returncode}")
            if not block or len(running) < args.jobs:
                return
            time.sleep(0.5)

    for out, cmd, venv in jobs:
        reap(block=True)
        while True:
            used = gpu_memory_mib()
            if used is None or used < args.gpu_mem_mib or not running:
                break
            time.sleep(5)
            reap(block=False)
        out.parent.mkdir(parents=True, exist_ok=True)
        if out.exists():
            out.unlink()
        env = dict(os.environ)
        env.update(venv)
        log = open(out.with_suffix(".log"), "w")
        running.append((subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT, env=env), out))
        time.sleep(1.0)
    while running:
        reap(block=False)
        time.sleep(0.5)

    pairs = 0
    bad = len(failed)
    for line in failed:
        print("FAILED", line)
    names = [
        job_name(c, b, int(s), h)
        for c, _ in captures
        for b in args.backends.split(",")
        for s in args.scales.split(",")
        for h in hd_modes
    ]
    sides: list[tuple[str, Path]] = [(v, args.out / v) for v, _ in variants]
    if args.reference:
        base_name, base_dir = "reference", args.reference
        others = sides
    else:
        (base_name, base_dir), others = sides[0], sides[1:]
    for name in names:
        for vname, vdir in others:
            frames, differ = compare(base_dir / (name + ".txt"), vdir / (name + ".txt"))
            pairs += 1
            if frames == 0 or differ:
                bad += 1
            print(f"{name}: {base_name} against {vname}: {frames} frames, {differ} differ")
    if pairs == 0:
        print("one configuration and no reference: hashes written, nothing compared")
        return 2 if not failed else 1
    print(f"{pairs} comparisons, {bad} with a difference or a failure")
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
