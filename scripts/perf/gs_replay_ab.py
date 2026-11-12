"""A/B benchmark of the gs renderer on a recorded GS stream.

Replays one DRKGSCAP capture (PS2X_GS_CAPTURE, docs/reference/gs-capture.md) through gsr_replay several times,
alternating two environments, and reports for each the renderer thread's CPU time per frame, the frame time
distribution and the renderer counters, plus whether the pictures are the same. A capture replays the same frames
whatever the machine is doing, so the counters and the picture hashes are exact; the times are not, which is why the
runs alternate and the report gives the median and the range.

    python scripts/perf/gs_replay_ab.py --replay build/gs/Release/gsr_replay.exe --capture <cap.bin> \
        --scale 8 --texdir <play folder>/textures --frames 1500-2500 \
        --a GSR_RT_LAZY_SYNC=0 --a GSR_RT_UPLOAD_GPU=0 --repeats 3

--a / --b take KEY=VALUE pairs for the two sides (side B defaults to the plain environment). --a-replay runs side A
with another gsr_replay binary (an older build) instead. The capture is hashed before and after: a run whose input
changed is reported and must be repeated.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import statistics
import subprocess
import sys
import tempfile
from pathlib import Path


def file_md5(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as stream:
        while block := stream.read(1 << 24):
            digest.update(block)
    return digest.hexdigest()


BENCH = re.compile(
    r"\[bench\] frames \d+-\d+ \((\d+)\): renderer thread wall ms/frame mean ([\d.]+) median ([\d.]+) "
    r"p95 ([\d.]+) p99 ([\d.]+) max ([\d.]+); thread cpu ms/frame ([\d.]+)"
)
REFRESH = re.compile(r"target refresh KiB ([\d.]+)")


def run(replay: Path, args: argparse.Namespace, env_pairs: list[str], hashes: Path) -> dict:
    env = dict(os.environ)
    for pair in env_pairs:
        key, _, value = pair.partition("=")
        env[key] = value
    command = [str(replay), str(args.capture), "--scale", str(args.scale), "--no-png", "--hashes", str(hashes)]
    command += ["--every", str(args.every), "--bench", args.frames]
    if args.texdir:
        command += ["--texdir", str(args.texdir)]
    if args.texup:
        command += ["--texup", args.texup]
    done = subprocess.run(command, env=env, capture_output=True, text=True, errors="replace", check=False)
    text = done.stdout + done.stderr
    match = BENCH.search(text)
    if done.returncode != 0 or not match:
        sys.exit(f"gsr_replay failed ({done.returncode}):\n{text[-2000:]}")
    result = {
        "frames": int(match.group(1)),
        "wall_mean": float(match.group(2)),
        "wall_median": float(match.group(3)),
        "wall_p95": float(match.group(4)),
        "cpu": float(match.group(7)),
        "refresh_kib": float(m.group(1)) if (m := REFRESH.search(text)) else 0.0,
        "counters": next((line for line in text.splitlines() if line.startswith("[bench] per frame")), ""),
        "hashes": hashes.read_text() if hashes.exists() else "",
    }
    return result


def spread(values: list[float]) -> str:
    return f"{statistics.median(values):.3f} ({min(values):.3f} to {max(values):.3f})"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--replay", type=Path, required=True, help="gsr_replay executable (side B, and A by default)")
    parser.add_argument("--a-replay", type=Path, help="gsr_replay executable for side A (an older build)")
    parser.add_argument("--capture", type=Path, required=True)
    parser.add_argument("--scale", type=int, default=8, help="internal resolution scale (default 8)")
    parser.add_argument("--texdir", type=Path, help="HD texture folder (the play folder's textures)")
    parser.add_argument("--texup", help="texture upscale factor passed to gsr_replay (auto, 1, 2, 4, 8)")
    parser.add_argument("--frames", default="0-999999", help="vsync frames to time, A-B (default: all)")
    parser.add_argument("--every", type=int, default=20, help="hash the display of every Nth frame (default 20)")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--a", action="append", default=[], metavar="KEY=VALUE", help="environment of side A")
    parser.add_argument("--b", action="append", default=[], metavar="KEY=VALUE", help="environment of side B")
    args = parser.parse_args()

    before = file_md5(args.capture)
    sides = {"A": (args.a_replay or args.replay, args.a), "B": (args.replay, args.b)}
    results: dict[str, list[dict]] = {"A": [], "B": []}
    with tempfile.TemporaryDirectory() as scratch:
        for repeat in range(args.repeats):
            for name, (replay, env_pairs) in sides.items():
                results[name].append(run(replay, args, env_pairs, Path(scratch) / f"{name}{repeat}.txt"))
                last = results[name][-1]
                print(f"run {repeat + 1} {name}: cpu {last['cpu']:.3f} ms/frame, wall median {last['wall_median']:.3f}")
    after = file_md5(args.capture)

    print(f"\ncapture {args.capture.name}: md5 {before}" + ("" if before == after else f" CHANGED to {after}"))
    for name, (replay, env_pairs) in sides.items():
        runs = results[name]
        print(f"\nside {name}: {replay.name} {' '.join(env_pairs) or '(plain environment)'}")
        print(f"  frames timed            {runs[0]['frames']}")
        print(f"  thread cpu ms/frame     {spread([r['cpu'] for r in runs])}")
        print(f"  wall ms/frame, median   {spread([r['wall_median'] for r in runs])}")
        print(f"  wall ms/frame, mean     {spread([r['wall_mean'] for r in runs])}")
        print(f"  wall ms/frame, p95      {spread([r['wall_p95'] for r in runs])}")
        print(f"  target refresh KiB/frame {spread([r['refresh_kib'] for r in runs])}")
        print(f"  {runs[0]['counters']}")
    reference = results["A"][0]["hashes"]
    stable = all(r["hashes"] == results[n][0]["hashes"] for n in results for r in results[n])
    same = stable and results["B"][0]["hashes"] == reference
    count = len(reference.splitlines())
    print(f"\npictures: {count} frames hashed per run; repeats identical: {stable}; A and B identical: {same}")
    return 0 if same and before == after else 1


if __name__ == "__main__":
    sys.exit(main())
