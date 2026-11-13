"""Interleaved A/B benchmark of gameplay scenes: frame times and CPU per world frame for several settings of one exe.

    python scripts/perf/cpu_scene_bench.py --exe <play folder>/drakan.exe --out-root build/bench --name ruined-lanes \\
        --scene ootf-ruined --variant strict:DRAKAN_VU_LANES_STRICT=1 --variant default --repeats 3
    python scripts/perf/cpu_scene_bench.py --exe ... --name surdana --scene surdana --variant base --repeats 3
    python scripts/perf/cpu_scene_bench.py --summarise build/bench/ruined-lanes     # tables again, no game runs

Every run is one `scripts/harness/play_run.py` game (New Game on an empty card, the scene's level, an optional `place`),
with `PS2X_PERF=1`, god mode and `DRAKAN_FRAME_AUDIT=2`. The recipe waits `--settle` seconds after control, then takes
two renderer screenshots `--seconds` apart; only the log between those two marks is measured. Runs go A B A B ... so
the variants see the same machine load on average; the number of other `drakan.exe` at the start and end of every run
is recorded and printed (a busy machine changes every wall-clock number here).

Per run: world frames per second (`flips2/s`, median of the one-second windows), the world frame interval
distribution from the `[ft] F` lines (median, the 99 % and 99.9 % intervals, i.e. the "1 % low" and "0.1 % low" as
times, and the longest), CPU milliseconds of the EE thread and of all other threads per world frame (thread CPU time,
not wall time: the stable figures under load; the VU1 worker's own CPU time too, from its thread name in the perf
line), VU1 pairs per frame and the busiest other threads. Per variant: the
median over the repeats and the range. A difference smaller than the range between repeats is not a result.

Use one settings file for all variants (`--ini`, passed as DRAKAN_INI) and say which one with the numbers: the world
rate, vsync, the frame limit and the window size all change them. The exe and its `.map` must come from the same build
when a profile is taken as well (`--env PS2X_HOSTPROF=<seconds> --env PS2X_HOSTPROF_DELAY=<seconds after the process
starts>`, see docs/reference/perf.md; `hostprof*.csv` is moved into each run's folder).
"""

from __future__ import annotations

import argparse
import itertools
import json
import re
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]

# level: play_run --level; place: "x y z [yaw]"; env: extra environment; mods: needs --mods (a converted pack folder)
SCENES = {
    "surdana": {"note": "Level 1A start (village, outdoors)"},
    "arena-1e": {"level": "1e", "place": "579166 128750 440175", "note": "Inquisitor's Lair arena spot (interior)"},
    "catacombs": {"level": "1c", "note": "Level 1C start (cave)"},
    "tundra-c": {"level": "4b", "place": "751616 140546 1224542", "note": "Northern Tundra spot C (heavy outdoors)"},
    "ravenshold": {"level": "4a", "note": "Level 4A start, mounted on Arokh over the valley"},
    "ootf-ruined": {"env": {"DRAKAN_GAME": "d1"}, "note": "Order of the Flame campaign start (Ruined Village)"},
    "ootf-volcanic": {
        "env": {"DRAKAN_START_LEVEL": "OotF 11 - Volcanic", "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro", "DRAKAN_GAME": "d2"},
        "mods": True,
        "note": "Order of the Flame Volcanic, direct start (needs --mods with a pack whose Volcanic loads)",
    },
    # Order of the Flame levels by direct start (the editor's Play): New Game loads the level in the intro's slot. As
    # Drakan 2 the campaign's 8 MiB texture cache is not raised; without it the start can stay on the loading screen.
    "ootf-canyons": {
        "env": {"DRAKAN_START_LEVEL": "OotF 03 - Wartok Canyons", "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro", "DRAKAN_GAME": "d2", "DRAKAN_TEX_CACHE_KB": "8192"},
        "mods": True,
        "note": "Order of the Flame Wartok Canyons, direct start (outdoors)",
    },
    "ootf-islands": {
        "env": {"DRAKAN_START_LEVEL": "OotF 06 - Islands", "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro", "DRAKAN_GAME": "d2", "DRAKAN_TEX_CACHE_KB": "8192"},
        "mods": True,
        "note": "Order of the Flame Islands, direct start (outdoors, large view)",
    },
    "ootf-mines": {
        "env": {"DRAKAN_START_LEVEL": "OotF 04 - Grimstone Mines", "DRAKAN_START_LEVEL_FROM": "Level 0 - Intro", "DRAKAN_GAME": "d2", "DRAKAN_TEX_CACHE_KB": "8192"},
        "mods": True,
        "note": "Order of the Flame Grimstone Mines, direct start (interior)",
    },
}  # fmt: skip
PERF = re.compile(r"^\[perf \d+\] (.*)$")
SHOT = re.compile(r"^\[in\] screenshot (bench0|bench1) -> ")
FLIP = re.compile(r"^\[ft\] F (\d+)")
THREAD = re.compile(r" t(\d+)(?:\(([^)]*)\))?:(\d+)%")


def recipe(settle: float, seconds: float, place: str | None) -> str:
    start = int(settle * 1000)
    lines = ["clock milliseconds", "0 sync play 1"]
    if place:
        lines.append(f"500 place {place}")
    lines += [
        f"{start} shot bench0",
        f"{start + int(seconds * 1000)} shot bench1",
        f"{start + int(seconds * 1000) + 1500} quit",
    ]
    return "\n".join(lines) + "\n"


def others() -> int:
    try:
        out = subprocess.run(
            ["tasklist", "/FI", "IMAGENAME eq drakan.exe", "/NH"], capture_output=True, text=True
        ).stdout
    except OSError:
        return -1
    return sum(1 for line in out.splitlines() if line.lower().startswith("drakan.exe"))


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def measure(log: Path) -> dict | None:
    """The numbers of one run from its drakan.log (None when the two marks are missing)."""
    inside, windows, flips, threads, named = False, [], [], {}, {}
    done = False
    for line in log.read_text(errors="replace").splitlines():
        mark = SHOT.match(line)
        if mark:
            inside = mark.group(1) == "bench0"
            done = done or mark.group(1) == "bench1"
            continue
        if not inside:
            continue
        flip = FLIP.match(line)
        if flip:
            flips.append(int(flip.group(1)) / 1000.0)
            continue
        perf = PERF.match(line)
        if perf:
            row = {k: float(v) for k, v in re.findall(r"([\w/]+)=([\d.]+)", perf.group(1))}
            windows.append(row)
            for tid, name, load in THREAD.findall(perf.group(1)):
                threads.setdefault(tid, []).append(float(load))
                if name and len(windows) > 1:  # the first window is dropped below
                    named[name] = named.get(name, 0.0) + float(load)
    windows = windows[1:]  # the first window still holds the screenshot and the frames before the mark
    if not done or len(windows) < 3:
        return None
    frames = sum(w["flips2/s"] for w in windows)
    result = {
        "windows": len(windows),
        "world_fps": statistics.median(w["flips2/s"] for w in windows),
        "world_fps_min": min(w["flips2/s"] for w in windows),
        "ee_ms_per_frame": 10.0 * sum(w["ee_thread_cpu"] for w in windows) / frames if frames else 0.0,
        "other_ms_per_frame": 10.0 * sum(w["other_cpu"] for w in windows) / frames if frames else 0.0,
        "ee_thread_pct": statistics.mean(w["ee_thread_cpu"] for w in windows),
        "vu1_mpairs_per_frame": sum(w.get("vu1Mpairs/s", 0.0) for w in windows) / frames if frames else 0.0,
        "dma_chain_mb_per_frame": sum(w.get("dma_chainMB/s", 0.0) for w in windows) / frames if frames else 0.0,
        "busiest_other_threads_pct": sorted((round(sum(v) / len(windows)) for v in threads.values()), reverse=True)[:4],
        # CPU of the threads that name themselves (a thread under 2 % of a core in a window is not listed there)
        "named_thread_ms_per_frame": {n: 10.0 * load / frames for n, load in named.items()} if frames else {},
    }
    result["vu1_ms_per_frame"] = result["named_thread_ms_per_frame"].get("VU1Worker", 0.0)
    gaps = [b - a for a, b in itertools.pairwise(flips)][5:]  # ms; skip the frames around the first screenshot
    if len(gaps) >= 100:
        result.update(
            frame_ms_median=statistics.median(gaps),
            frame_ms_p99=percentile(gaps, 0.99),
            frame_ms_p999=percentile(gaps, 0.999),
            frame_ms_max=max(gaps),
            frames=len(gaps),
        )
    return result


COLUMNS = [
    ("world_fps", "world fps", "{:.1f}"),
    ("frame_ms_median", "frame ms", "{:.2f}"),
    ("frame_ms_p99", "1% ms", "{:.1f}"),
    ("frame_ms_p999", "0.1% ms", "{:.1f}"),
    ("frame_ms_max", "max ms", "{:.0f}"),
    ("ee_ms_per_frame", "EE cpu ms/frame", "{:.2f}"),
    ("other_ms_per_frame", "other cpu ms/frame", "{:.2f}"),
    ("vu1_ms_per_frame", "VU1 worker cpu ms/frame", "{:.2f}"),
    ("vu1_mpairs_per_frame", "VU1 Mpairs/frame", "{:.2f}"),
]


def report(folder: Path) -> int:
    runs = json.loads((folder / "runs.json").read_text())
    by_variant: dict[str, list[dict]] = {}
    for run in runs:
        by_variant.setdefault(run["variant"], []).append(run)
    print(f"{folder.name}: scene {runs[0]['scene']}, {len(runs)} runs")
    print("| variant | runs | " + " | ".join(title for _, title, _ in COLUMNS) + " | other games (start/end) |")
    print("| --- | --- | " + " | ".join("---" for _ in COLUMNS) + " | --- |")
    summary = {}
    for variant, rows in by_variant.items():
        good = [r["result"] for r in rows if r["result"]]
        cells, summary[variant] = [], {}
        for key, _, fmt in COLUMNS:
            values = [g[key] for g in good if key in g]
            if not values:
                cells.append("n/a")
                continue
            summary[variant][key] = {"median": statistics.median(values), "min": min(values), "max": max(values)}
            cells.append(
                f"{fmt.format(statistics.median(values))} ({fmt.format(min(values))} to {fmt.format(max(values))})"
            )
        load = ", ".join(f"{r['others_start']}/{r['others_end']}" for r in rows)
        print(f"| {variant} | {len(good)} of {len(rows)} | " + " | ".join(cells) + f" | {load} |")
    (folder / "summary.json").write_text(json.dumps(summary, indent=2))
    return 0 if all(r["result"] for r in runs) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--summarise", type=Path, help="print the tables of an earlier benchmark folder and exit")
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--out-root", type=Path, default=ROOT / "build" / "bench")
    ap.add_argument("--name", default="bench")
    ap.add_argument("--scene", default="surdana", choices=sorted(SCENES))
    ap.add_argument("--place", help='own "x y z [yaw]" instead of the scene\'s')
    ap.add_argument("--mods", type=Path, help="DRAKAN_MODS folder for the run (required by some scenes)")
    ap.add_argument(
        "--ini", type=Path, help="settings file for every run (DRAKAN_INI); default: the exe folder's drakan.ini"
    )
    ap.add_argument("--variant", action="append", default=[], help="NAME or NAME:KEY=VALUE[,KEY=VALUE...]; repeatable")
    ap.add_argument("--env", action="append", default=[], help="KEY=VALUE for every run")
    ap.add_argument("--repeats", type=int, default=3)
    ap.add_argument("--settle", type=float, default=8.0, help="seconds after control before the measured window")
    ap.add_argument("--seconds", type=float, default=20.0, help="length of the measured window")
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--audio", action="store_true", help="keep audio on (play_run turns it off by default)")
    a = ap.parse_args()
    if a.summarise:
        return report(a.summarise)
    if not a.exe:
        ap.error("--exe is required")
    scene = SCENES[a.scene]
    if scene.get("mods") and not a.mods:
        ap.error(f"scene {a.scene} needs --mods")
    variants = []
    for spec in a.variant or ["base"]:
        name, _, rest = spec.partition(":")
        variants.append((name, [kv for kv in rest.split(",") if kv]))
    folder = (a.out_root / a.name).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    recipe_path = folder / "recipe.txt"
    recipe_path.write_text(recipe(a.settle, a.seconds, a.place or scene.get("place")), encoding="utf-8")
    common = ["PS2X_PERF=1", "DRAKAN_PLAYTEST_GOD=1", "DRAKAN_FRAME_AUDIT=2", *a.env]
    common += [f"{k}={v}" for k, v in scene.get("env", {}).items()]
    if a.ini:
        common.append(f"DRAKAN_INI={a.ini.resolve()}")
    if a.mods:
        common.append(f"DRAKAN_MODS={a.mods.resolve()}")
    runs = []
    for repeat in range(a.repeats):
        for name, extra in variants:
            run = f"{name}-{repeat}"
            cmd = [sys.executable, str(ROOT / "scripts" / "harness" / "play_run.py"), "--exe", str(a.exe),
                   "--out-root", str(folder), "--name", run, "--recipe", str(recipe_path), "--timeout", str(a.timeout),
                   "--attempts", "1"]  # fmt: skip
            if scene.get("level"):
                cmd += ["--level", scene["level"]]
            if a.audio:
                cmd.append("--audio")
            for kv in common + extra:
                cmd += ["--env", kv]
            before = others()
            code = subprocess.run(cmd, capture_output=True, text=True).returncode
            log = folder / run / "drakan.log"
            for profile in ("hostprof.csv", "hostprof_stacks.csv"):  # PS2X_HOSTPROF writes next to the exe
                if (a.exe.parent / profile).is_file() and (folder / run).is_dir():
                    shutil.move(a.exe.parent / profile, folder / run / profile)
            result = measure(log) if log.is_file() else None
            runs.append({"variant": name, "repeat": repeat, "scene": a.scene, "env": common + extra, "exit": code,
                         "others_start": before, "others_end": others(), "result": result})  # fmt: skip
            fps = f"{result['world_fps']:.1f} world fps" if result else "no measurement (see the run's drakan.log)"
            print(f"{run}: exit {code}, {fps}", flush=True)
            (folder / "runs.json").write_text(json.dumps(runs, indent=2))
    return report(folder)


if __name__ == "__main__":
    sys.exit(main())
