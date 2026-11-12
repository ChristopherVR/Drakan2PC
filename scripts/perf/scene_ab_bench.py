"""Interleaved A/B benchmark of one scene on a quiet machine: environment variants across runs, a live setting cycled
inside each run, optionally with the game as the foreground window.

    python scripts/perf/scene_ab_bench.py --exe <play>/drakan.exe --out-root build/bench --name surdana-cpu \\
        --scene surdana --repeats 5 --cycle video.dlss=quality,off \\
        --variant old:DRAKAN_VU_LANES_STRICT=1,DRAKAN_SCENE_CAPTURE_FAST=0,PS2X_DMA_CHAIN_POOL=0 --variant new
    python scripts/perf/scene_ab_bench.py --summarise build/bench/surdana-cpu      # the tables again, no game runs

Every run is one `scripts/harness/play_run.py` game (New Game on an empty card, the scene's level and pose). At the
pose the recipe sets each mode of `--cycle KEY=a,b,...` through the live settings path (not written to the ini),
waits `--settle` seconds and measures `--window` seconds between two renderer screenshots; the order of the modes
rotates with the repeat. Without `--cycle` there is one window per run. Runs go A B A B ... with the variant order
rotated per repeat, so the variants see the same machine on average.

Before every run the tool waits (up to `--wait-quiet` seconds) until at most `--quiet-games` other `drakan.exe` and no
replay tool (`gsr_*`) or compiler (`cl.exe`, `link.exe`) is running, and records what ran before and during the run.
A run that started on a busy machine is marked and left out of the tables unless `--keep-busy` is given. Runs of
every copy of this tool on the machine take turns (a lock in the temp folder), so two of our own games never measure
each other.

`--foreground` runs the game as the activated, focused window (`DRAKAN_NO_ACTIVATE=0`; the harness default keeps the
window behind the others and never activates it) and records how often the foreground window belonged to the game
during the run. A borderless window in the foreground can be presented without the desktop compositor in the path;
compare a `--foreground` benchmark with the same one without it before trusting absolute frame rates.

Per window: world frames per second and the interval distribution of the `[ft] F` lines (median, 99 % and 99.9 %
intervals, i.e. the 1 % and 0.1 % lows as times, and the longest), CPU time per world frame of the EE thread and of
every thread that names itself (`[perf]`), presents per second, time inside `Present` per call (`GSR_PROFILE=2`) and
GPU time per pass (`GSR_GPU_PASS_TIMING=1`). Per variant and mode: median over the repeats with the range, and for
two variants the per-repeat pairs. A difference smaller than the range between repeats is not a result.
"""

from __future__ import annotations

import argparse
import ctypes
import json
import os
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
from cpu_scene_bench import SCENES as BASE_SCENES  # noqa: E402

# Scenes of cpu_scene_bench.py plus two that need more than a pose. `script` lines are (milliseconds after the pose,
# input script command) and repeat every `period` ms for the whole run when `period` is given.
SCENES = dict(BASE_SCENES)
SCENES.update(
    {
        "surdana-town": {
            "place": "451816 161119 500435 3.1416",
            "note": "Level 1A, Surdana town square: houses, trees, the bridge and two or three villagers in view",
        },
        "surdana-fight": {
            "place": "441400 174950 384300 3.1416",
            "note": "Level 1A, three Wartoks attacking Rynn at the cave mouth (god mode); she swings every 0.7 s",
            "period": 700,
            "script": [(0, "button down 1"), (90, "button up 1")],
        },
    }
)

SHOT = re.compile(r"^\[in\] screenshot ([ab])_([\w.-]+?)_(\d+) -> ")
FLIP = re.compile(r"^\[ft\] F (\d+)")
PERF = re.compile(r"^\[perf \d+\] (.*)$")
THREAD = re.compile(r" t(\d+)(?:\(([^)]*)\))?:(\d+)%")
PROFILE = re.compile(r"^\[gsr-profile\] ([\d.]+)s root=([\d.]+)ms .*?: (.*)$")
SCOPE = re.compile(r"(\w+)=(\d+)/([\d.]+)/([\d.]+)/([\d.]+)")
GPU = re.compile(r"^\[gpu-pass\] (.*)$")
PER_WORLD = ("rb/s", "finish_polls/s", "rb_wait_ms/s", "rb_eager/s", "rb_late/s", "rb_verified/s", "rb_diff/s",
             "vu1_syncs/s", "vu1_sync_ms/s", "dma_chains/s")  # fmt: skip
CPU_SCOPES = ("presentCall", "draw", "drawObjectMotion", "syncRT", "readbackMap", "surfaceWait")
GPU_PASSES = ("world", "worldEntry", "guides", "modelMotion", "evaluate", "compose", "display", "presentBlit", "overlay", "total")  # fmt: skip


def tasks() -> list[tuple[str, int]]:
    try:
        out = subprocess.run(["tasklist", "/NH", "/FO", "CSV"], capture_output=True, text=True).stdout
    except OSError:
        return []
    rows = []
    for line in out.splitlines():
        parts = [p.strip('"') for p in line.split('","')]
        if len(parts) >= 2 and parts[1].isdigit():
            rows.append((parts[0].lower(), int(parts[1])))
    return rows


def load(own: set[int] = frozenset()) -> dict:
    """Other games, replay tools and compilers running now, and the GPU utilisation."""
    rows = [r for r in tasks() if r[1] not in own]
    try:
        gpu = int(
            subprocess.run(
                ["nvidia-smi", "--query-gpu=utilization.gpu", "--format=csv,noheader,nounits"],
                capture_output=True,
                text=True,
            ).stdout.split()[0]
        )
    except (OSError, ValueError, IndexError):
        gpu = -1
    return {
        "games": sum(n == "drakan.exe" for n, _ in rows),
        "replays": sum(n.startswith("gsr_") for n, _ in rows),
        "compilers": sum(n in ("cl.exe", "link.exe") for n, _ in rows),
        "gpu_pct": gpu,
    }


def quiet(state: dict, games: int) -> bool:
    return state["games"] <= games and state["replays"] == 0 and state["compilers"] == 0


LOCK = Path(tempfile.gettempdir()) / "drakan_scene_bench.lock"


def pid_alive(pid: int) -> bool:
    return any(p == pid for _name, p in tasks())


def take_lock() -> None:
    """One benchmark game at a time on this machine, whichever copy of this tool starts it: two games of our own
    would measure each other. The lock is a folder holding the owner's process id; a dead owner's lock is removed."""
    while True:
        try:
            LOCK.mkdir()
            (LOCK / "pid").write_text(str(os.getpid()))
            return
        except FileExistsError:
            try:
                owner = int((LOCK / "pid").read_text())
            except (OSError, ValueError):
                owner = -1
            if owner > 0 and not pid_alive(owner):
                shutil.rmtree(LOCK, ignore_errors=True)
                continue
            time.sleep(5)


def release_lock() -> None:
    shutil.rmtree(LOCK, ignore_errors=True)


def foreground_pid() -> int:
    user32 = ctypes.windll.user32
    pid = ctypes.c_ulong(0)
    user32.GetWindowThreadProcessId(user32.GetForegroundWindow(), ctypes.byref(pid))
    return pid.value


def exe_of(pid: int) -> str:
    """Full path of a process image (empty when it cannot be opened)."""
    kernel32 = ctypes.windll.kernel32
    handle = kernel32.OpenProcess(0x1000, False, pid)  # PROCESS_QUERY_LIMITED_INFORMATION
    if not handle:
        return ""
    try:
        size = ctypes.c_ulong(1024)
        buffer = ctypes.create_unicode_buffer(size.value)
        ok = kernel32.QueryFullProcessImageNameW(handle, 0, buffer, ctypes.byref(size))
        return buffer.value if ok else ""
    finally:
        kernel32.CloseHandle(handle)


def watch(stop: threading.Event, exe: Path, samples: list, fg: list) -> None:
    """Samples the machine load and whether the game owns the foreground window while a run is going. Our game is
    the drakan.exe that runs from the benchmarked exe's path."""
    want = str(exe.resolve()).lower()
    while not stop.wait(2):
        ours = {pid for n, pid in tasks() if n == "drakan.exe" and exe_of(pid).lower() == want}
        samples.append(load(ours))
        if ours:
            fg.append(foreground_pid() in ours)


def recipe(a: argparse.Namespace, scene: dict, repeat: int) -> tuple[str, list[str]]:
    lines = ["clock milliseconds", "0 sync play 1"]
    place = a.place or scene.get("place")
    if place:
        lines.append(f"500 place {place}")
    t = int(a.lead * 1000)
    key, modes = a.cycle_key, list(a.cycle_modes)
    order = modes[repeat % len(modes) :] + modes[: repeat % len(modes)]
    marks = []
    for mode in order:
        if key:
            marks.append((t, f"setting {key} {mode}"))
        t += int(a.settle * 1000)
        marks.append((t, f"shot a_{mode}_{repeat}"))
        t += int(a.window * 1000)
        marks.append((t, f"shot b_{mode}_{repeat}"))
        t += 300
    end = t + 1200
    extra = []
    if scene.get("script"):
        period = scene.get("period")
        start = 1500
        while start < end - 200:
            extra += [(start + offset, cmd) for offset, cmd in scene["script"]]
            if not period:
                break
            start += period
    for at, cmd in sorted(marks + extra, key=lambda m: m[0]):
        lines.append(f"{at} {cmd}")
    lines.append(f"{end} quit")
    return "\n".join(lines) + "\n", order


def percentile(ordered: list[float], fraction: float) -> float:
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def measure(log: Path) -> dict:
    """Per mode of the run: the numbers of its window (the log between `shot a_<mode>` and `shot b_<mode>`)."""
    windows, cur = {}, None
    for line in log.read_text(errors="replace").splitlines():
        s = SHOT.match(line)
        if s:
            cur = None
            if s.group(1) == "a":
                cur = windows[s.group(2)] = {"flips": [], "perf": [], "profile": [], "gpu": [], "closed": False}
            elif s.group(2) in windows:
                windows[s.group(2)]["closed"] = True
            continue
        if cur is None:
            continue
        f = FLIP.match(line)
        if f:
            cur["flips"].append(int(f.group(1)) / 1000.0)
            continue
        p = PERF.match(line)
        if p:
            row = {k: float(v) for k, v in re.findall(r"([\w/]+)=([\d.]+)", p.group(1))}
            row["threads"] = {name: float(v) for _tid, name, v in THREAD.findall(p.group(1)) if name}
            cur["perf"].append(row)
            continue
        g = PROFILE.match(line)
        if g:
            row = {"seconds": float(g.group(1))}
            for name, calls, inclusive, _exclusive, longest in SCOPE.findall(g.group(3)):
                row[name] = (int(calls), float(inclusive), float(longest))
            cur["profile"].append(row)
            continue
        u = GPU.match(line)
        if u:
            cur["gpu"].append({k: float(v) for k, v in re.findall(r"(\w+)=([\d.]+)", u.group(1))})
    out = {}
    for mode, w in windows.items():
        gaps = [b - a for a, b in zip(w["flips"], w["flips"][1:])][10:]  # the frames around the first screenshot
        perf, prof, gpu = w["perf"][1:], w["profile"][1:], w["gpu"][1:]
        if not w["closed"] or len(gaps) < 50:
            continue
        ordered = sorted(gaps)
        r = {
            "frames": len(gaps),
            "world_fps": 1000.0 * len(gaps) / sum(gaps),
            "frame_ms_median": statistics.median(gaps),
            "frame_ms_p99": percentile(ordered, 0.99),
            "frame_ms_p999": percentile(ordered, 0.999),
            "frame_ms_max": ordered[-1],
        }
        worlds = sum(p["flips2/s"] for p in perf)
        if perf and worlds:
            r["presents_per_s"] = statistics.median(p["flips/s"] for p in perf)
            r["ee_ms_per_world"] = 10.0 * sum(p["ee_thread_cpu"] for p in perf) / worlds
            r["other_ms_per_world"] = 10.0 * sum(p["other_cpu"] for p in perf) / worlds
            r["vu1_mpairs_per_world"] = sum(p.get("vu1Mpairs/s", 0.0) for p in perf) / worlds
            names: dict[str, float] = {}
            for p in perf:
                for name, v in p["threads"].items():
                    names[name] = names.get(name, 0.0) + v
            r["thread_ms_per_world"] = {name: 10.0 * v / worlds for name, v in names.items()}
            # counters of the perf line that are rates per second: per world frame (load-independent counts)
            r["per_world"] = {key: sum(p.get(key, 0.0) for p in perf) / worlds for key in PER_WORLD}
        if prof:
            seconds = sum(p["seconds"] for p in prof)
            presents = sum(p.get("presentCall", (0, 0, 0))[0] for p in prof)
            r["profile_presents_per_s"] = presents / seconds
            for name in CPU_SCOPES:
                total = sum(p.get(name, (0, 0, 0))[1] for p in prof)
                r[f"cpu_{name}_ms_per_s"] = total / seconds
            r["present_ms_per_call"] = (
                sum(p.get("presentCall", (0, 0, 0))[1] for p in prof) / presents if presents else 0.0
            )
            r["readbacks_per_s"] = sum(p.get("readbackMap", (0, 0, 0))[0] for p in prof) / seconds
        if gpu:
            r["gpu_ms_per_world"] = {
                k: statistics.median(g[k] for g in gpu if k in g) for k in GPU_PASSES if any(k in g for g in gpu)
            }
        out[mode] = r
    return out


COLUMNS = [
    ("world_fps", "world fps", "{:.1f}"),
    ("frame_ms_median", "frame ms", "{:.2f}"),
    ("frame_ms_p99", "1 % low ms", "{:.1f}"),
    ("frame_ms_p999", "0.1 % low ms", "{:.1f}"),
    ("frame_ms_max", "longest ms", "{:.0f}"),
    ("ee_ms_per_world", "EE cpu ms", "{:.2f}"),
    ("thread:VU1Worker", "VU1 cpu ms", "{:.2f}"),
    ("thread:GSWorker", "GS cpu ms", "{:.2f}"),
    ("other_ms_per_world", "all other cpu ms", "{:.2f}"),
    ("presents_per_s", "presents/s", "{:.0f}"),
    ("present_ms_per_call", "Present ms", "{:.2f}"),
    ("pw:rb/s", "read-backs", "{:.3f}"),
    ("pw:finish_polls/s", "FINISH polls", "{:.0f}"),
    ("pw:rb_wait_ms/s", "read wait ms", "{:.2f}"),
    ("pw:vu1_sync_ms/s", "VU1 sync wait ms", "{:.2f}"),
    ("pw:rb_late/s", "late reads", "{:.3f}"),
    ("pw:rb_diff/s", "late reads that differ", "{:.4f}"),
    ("gpu:world", "GPU world ms", "{:.2f}"),
    ("gpu:total", "GPU total ms", "{:.2f}"),
]


def value(result: dict, key: str) -> float | None:
    if key.startswith("thread:"):
        return result.get("thread_ms_per_world", {}).get(key[7:])
    if key.startswith("pw:"):
        return result.get("per_world", {}).get(key[3:])
    if key.startswith("gpu:"):
        return result.get("gpu_ms_per_world", {}).get(key[4:])
    return result.get(key)


def cell(values: list[float | None], fmt: str) -> str:
    values = [v for v in values if v is not None]
    if not values:
        return "n/a"
    return f"{fmt.format(statistics.median(values))} ({fmt.format(min(values))} to {fmt.format(max(values))})"


def report(folder: Path, keep_busy: bool = False) -> int:
    data = json.loads((folder / "runs.json").read_text())
    runs = data["runs"]
    used = [r for r in runs if r["windows"] and (keep_busy or r["quiet_at_start"])]
    print(f"{folder.name}: scene {data['scene']} ({SCENES[data['scene']].get('note', '')})")
    print(f"foreground requested: {data['foreground']}; runs {len(runs)}, used {len(used)}")
    for r in runs:
        fg = (
            ""
            if r["foreground_share"] is None
            else f", game in foreground {100 * r['foreground_share']:.0f} % of samples"
        )
        print(
            f"  {r['variant']}-{r['repeat']}: exit {r['exit']}, before {r['load_before']}, waited {r['waited_s']:.0f} s, "
            f"peak during {r['load_peak']}{fg}{'' if r['quiet_at_start'] else '  ** busy at start **'}"
        )
    variants = list(dict.fromkeys(r["variant"] for r in runs))
    modes = list(dict.fromkeys(m for r in runs for m in r["windows"]))
    summary: dict = {}
    for mode in modes:
        print(f"\nmode {mode}: median over the repeats (lowest to highest); CPU columns are per world frame")
        print("| variant | runs | " + " | ".join(title for _, title, _ in COLUMNS) + " |")
        print("| --- | --- | " + " | ".join("---" for _ in COLUMNS) + " |")
        for variant in variants:
            rows = [r["windows"][mode] for r in used if r["variant"] == variant and mode in r["windows"]]
            summary.setdefault(mode, {})[variant] = {key: [value(row, key) for row in rows] for key, _, _ in COLUMNS}
            print(f"| {variant} | {len(rows)} | " + " | ".join(cell([value(row, key) for row in rows], fmt) for key, _, fmt in COLUMNS) + " |")  # fmt: skip
        if len(variants) >= 2:
            base = variants[0]
            for other in variants[1:]:
                pairs = []
                for repeat in sorted({r["repeat"] for r in used}):
                    a = [r for r in used if r["repeat"] == repeat and r["variant"] == base and mode in r["windows"]]
                    b = [r for r in used if r["repeat"] == repeat and r["variant"] == other and mode in r["windows"]]
                    if a and b:
                        pairs.append(
                            (a[0]["windows"][mode]["frame_ms_median"], b[0]["windows"][mode]["frame_ms_median"])
                        )
                if pairs:
                    faster = sum(b < a for a, b in pairs)
                    change = [100.0 * (b - a) / a for a, b in pairs]
                    print(
                        f"pairs {base} -> {other}, median frame ms: "
                        + ", ".join(f"{a:.2f}->{b:.2f}" for a, b in pairs)
                        + f"; {other} faster in {faster} of {len(pairs)}, median change {statistics.median(change):+.1f} % "
                        f"({min(change):+.1f} to {max(change):+.1f})"
                    )
    (folder / "summary.json").write_text(json.dumps(summary, indent=1))
    return 0 if len(used) == len(runs) else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--summarise", type=Path, help="print the tables of an earlier benchmark folder and exit")
    ap.add_argument("--keep-busy", action="store_true", help="also use runs that started on a busy machine")
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--out-root", type=Path, default=ROOT / "build" / "bench")
    ap.add_argument("--name", default="bench")
    ap.add_argument("--scene", default="surdana", choices=sorted(SCENES))
    ap.add_argument("--place", help='own "x y z [yaw]" instead of the scene\'s')
    ap.add_argument("--mods", type=Path, help="DRAKAN_MODS folder for the run (required by some scenes)")
    ap.add_argument("--ini", type=Path, help="settings file for every run (DRAKAN_INI); default: the exe folder's")
    ap.add_argument("--save", type=Path, help="memory card folder to copy into each run (play_run --save)")
    ap.add_argument("--variant", action="append", default=[], help="NAME or NAME:KEY=VALUE[,KEY=VALUE...]; repeatable")
    ap.add_argument("--env", action="append", default=[], help="KEY=VALUE for every run")
    ap.add_argument(
        "--cycle", help="KEY=mode1,mode2,...: a live setting cycled inside each run (e.g. video.dlss=quality,off)"
    )
    ap.add_argument("--repeats", type=int, default=5)
    ap.add_argument("--lead", type=float, default=6.0, help="seconds after control before the first mode")
    ap.add_argument("--settle", type=float, default=3.0, help="seconds after a mode switch before its window")
    ap.add_argument("--window", type=float, default=8.0, help="length of each measured window")
    ap.add_argument("--timeout", type=float, default=300.0)
    ap.add_argument("--audio", action="store_true", help="keep audio on (play_run turns it off by default)")
    ap.add_argument("--foreground", action="store_true", help="activate and focus the game window (see above)")
    ap.add_argument("--quiet-games", type=int, default=1, help="other drakan.exe tolerated as quiet")
    ap.add_argument("--wait-quiet", type=float, default=900.0, help="seconds to wait for a quiet machine per run")
    a = ap.parse_args()
    if a.summarise:
        return report(a.summarise, a.keep_busy)
    if not a.exe:
        ap.error("--exe is required")
    scene = SCENES[a.scene]
    if scene.get("mods") and not a.mods:
        ap.error(f"scene {a.scene} needs --mods")
    a.cycle_key, a.cycle_modes = None, ["base"]
    if a.cycle:
        a.cycle_key, _, modes = a.cycle.partition("=")
        a.cycle_modes = [m for m in modes.split(",") if m]
    variants = []
    for spec in a.variant or ["base"]:
        name, _, rest = spec.partition(":")
        variants.append((name, [kv for kv in rest.split(",") if kv]))
    folder = (a.out_root / a.name).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    common = ["PS2X_PERF=1", "DRAKAN_PLAYTEST_GOD=1", "DRAKAN_FRAME_AUDIT=2", "GSR_PROFILE=2", "GSR_GPU_PASS_TIMING=1"]
    common += a.env + [f"{k}={v}" for k, v in scene.get("env", {}).items()]
    if a.foreground:
        common.append("DRAKAN_NO_ACTIVATE=0")
    if a.ini:
        common.append(f"DRAKAN_INI={a.ini.resolve()}")
    if a.mods:
        common.append(f"DRAKAN_MODS={a.mods.resolve()}")
    runs = []
    for repeat in range(a.repeats):
        order = variants[repeat % len(variants) :] + variants[: repeat % len(variants)]
        for name, extra in order:
            run = f"{name}-{repeat}"
            text, _modes = recipe(a, scene, repeat)
            recipe_path = folder / f"{run}.recipe.txt"
            recipe_path.write_text(text, encoding="utf-8")
            cmd = [sys.executable, str(ROOT / "scripts" / "harness" / "play_run.py"), "--exe", str(a.exe),
                   "--out-root", str(folder), "--name", run, "--recipe", str(recipe_path), "--timeout", str(a.timeout),
                   "--attempts", "1"]  # fmt: skip
            if scene.get("level"):
                cmd += ["--level", scene["level"]]
            if a.save:
                cmd += ["--save", str(a.save)]
            if a.audio:
                cmd.append("--audio")
            for kv in common + extra:
                cmd += ["--env", kv]
            waited = 0.0
            while True:  # wait for a quiet machine without holding the lock, then check again with it
                before = load()
                while not quiet(before, a.quiet_games) and waited < a.wait_quiet:
                    time.sleep(10)
                    waited += 10
                    before = load()
                take_lock()
                before = load()
                if quiet(before, a.quiet_games) or waited >= a.wait_quiet:
                    break
                release_lock()
            samples, fg, stop = [], [], threading.Event()

            watcher = threading.Thread(target=watch, args=(stop, a.exe, samples, fg), daemon=True)
            watcher.start()
            code = subprocess.run(cmd, capture_output=True, text=True).returncode
            stop.set()
            watcher.join()
            release_lock()
            log = folder / run / "drakan.log"
            for profile in ("hostprof.csv", "hostprof_stacks.csv"):  # PS2X_HOSTPROF writes next to the exe
                if (a.exe.parent / profile).is_file() and (folder / run).is_dir():
                    shutil.move(a.exe.parent / profile, folder / run / profile)
            peak = {k: max((s[k] for s in samples), default=-1) for k in ("games", "replays", "compilers")}
            runs.append({"variant": name, "repeat": repeat, "env": common + extra, "exit": code, "load_before": before,
                         "waited_s": waited, "quiet_at_start": quiet(before, a.quiet_games), "load_peak": peak,
                         "foreground_share": (sum(fg) / len(fg)) if fg else None,
                         "windows": measure(log) if log.is_file() else {}})  # fmt: skip
            got = runs[-1]["windows"]
            print(
                f"{run}: exit {code}, " + ", ".join(f"{m} {w['world_fps']:.1f} fps" for m, w in got.items()), flush=True
            )
            (folder / "runs.json").write_text(
                json.dumps({"scene": a.scene, "foreground": a.foreground, "cycle": a.cycle, "runs": runs}, indent=1)
            )
    return report(folder, a.keep_busy)


if __name__ == "__main__":
    sys.exit(main())
