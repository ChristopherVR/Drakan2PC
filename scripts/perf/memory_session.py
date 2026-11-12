"""Process and GPU memory of one long game session, with level changes.

    python scripts/perf/memory_session.py --exe <play>/drakan.exe --out-root build/bench --name mem30 --minutes 30 \\
        --levelchange "Level 1D - City of the Damned@6000,Level 1A - Surdana,Level 1D - City of the Damned"
    python scripts/perf/memory_session.py --summarise build/bench/mem30

Runs one `scripts/harness/play_run.py` game (New Game, god mode, the player stands still), samples every `--every`
seconds and quits after `--minutes`. `--levelchange` is the game's test aid DRAKAN_LEVELCHANGE (a level name per
change, the first with `@ticks` between changes): the game fires the matching level exit by itself. Sampled: private
bytes, working set, handles and threads of the process, and the Windows counters `GPU Process Memory` (dedicated and
shared) of its process id. The level the game is in comes from its lifecycle log (`afterLevelLoad`).

The table has one row per stay in a level: seconds, the lowest, highest and last value of each figure. Memory figures
do not depend on the machine load, the time between level changes does. The run takes the scene benchmark's lock, so
it never overlaps another benchmark game of ours.
"""

from __future__ import annotations

import argparse
import csv
import ctypes
import ctypes.wintypes
import json
import re
import subprocess
import sys
import threading
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(Path(__file__).resolve().parent))
import scene_ab_bench as bench  # noqa: E402

STAMP = re.compile(r"^\[\+(\d+\.\d+)\] (.*)")
LOADED = re.compile(r"\[lifecycle [\d.]+\] afterLevelLoad level='([^']*)'")
FIELDS = ["t", "private_mb", "working_set_mb", "gpu_dedicated_mb", "gpu_shared_mb", "handles", "threads"]


class Counters(ctypes.Structure):
    _fields_ = [("cb", ctypes.wintypes.DWORD), ("PageFaultCount", ctypes.wintypes.DWORD)] + [
        (name, ctypes.c_size_t)
        for name in ("PeakWorkingSetSize", "WorkingSetSize", "QuotaPeakPagedPoolUsage", "QuotaPagedPoolUsage",
                     "QuotaPeakNonPagedPoolUsage", "QuotaNonPagedPoolUsage", "PagefileUsage", "PeakPagefileUsage",
                     "PrivateUsage")
    ]  # fmt: skip


def process_memory(pid: int) -> tuple[int, int, int] | None:
    """(private bytes, working set, handle count) of a process."""
    kernel32, psapi = ctypes.windll.kernel32, ctypes.windll.psapi
    handle = kernel32.OpenProcess(0x1000 | 0x0010, False, pid)  # QUERY_LIMITED_INFORMATION | VM_READ
    if not handle:
        return None
    try:
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        if not psapi.GetProcessMemoryInfo(handle, ctypes.byref(counters), counters.cb):
            return None
        handles = ctypes.wintypes.DWORD(0)
        kernel32.GetProcessHandleCount(handle, ctypes.byref(handles))
        return counters.PrivateUsage, counters.WorkingSetSize, handles.value
    finally:
        kernel32.CloseHandle(handle)


def gpu_and_threads(pid: int) -> tuple[float, float, int]:
    """(GPU dedicated bytes, GPU shared bytes, thread count) from the Windows counters and the process list."""
    script = (
        f"$d=(Get-Counter '\\GPU Process Memory(pid_{pid}_*)\\Dedicated Usage' -ErrorAction SilentlyContinue)"
        ".CounterSamples | Measure-Object CookedValue -Sum;"
        f"$s=(Get-Counter '\\GPU Process Memory(pid_{pid}_*)\\Shared Usage' -ErrorAction SilentlyContinue)"
        ".CounterSamples | Measure-Object CookedValue -Sum;"
        f"$p=Get-Process -Id {pid} -ErrorAction SilentlyContinue;"
        '"$($d.Sum)|$($s.Sum)|$($p.Threads.Count)"'
    )
    out = subprocess.run(["powershell", "-NoProfile", "-Command", script], capture_output=True, text=True).stdout
    parts = out.strip().split("|")
    try:
        return float(parts[0] or 0), float(parts[1] or 0), int(parts[2] or 0)
    except (ValueError, IndexError):
        return 0.0, 0.0, 0


def sample(exe: Path, out: Path, every: float, stop: threading.Event) -> None:
    want = str(exe.resolve()).lower()
    start = time.perf_counter()
    with out.open("w", newline="") as f:
        writer = csv.writer(f)
        writer.writerow(FIELDS)
        while not stop.wait(every):
            pids = [pid for name, pid in bench.tasks() if name == "drakan.exe" and bench.exe_of(pid).lower() == want]
            if not pids:
                continue
            memory = process_memory(pids[0])
            if not memory:
                continue
            dedicated, shared, threads = gpu_and_threads(pids[0])
            writer.writerow([round(time.perf_counter() - start, 1), memory[0] >> 20, memory[1] >> 20,
                             int(dedicated) >> 20, int(shared) >> 20, memory[2], threads])  # fmt: skip
            f.flush()


def span(rows: list[dict], key: str) -> str:
    values = [r[key] for r in rows]
    return f"{min(values):.0f} / {max(values):.0f} / {values[-1]:.0f}"


def report(folder: Path) -> int:
    rows = [{k: float(v) for k, v in row.items()} for row in csv.DictReader((folder / "memory.csv").open())]
    loads = []
    for log in sorted((folder / "run").glob("stdout_*.log")):
        for raw in log.read_text(encoding="latin-1").splitlines():
            m = STAMP.match(raw)
            found = LOADED.search(m.group(2)) if m else None
            if found:
                loads.append((float(m.group(1)), found.group(1)))
    meta = json.loads((folder / "session.json").read_text()) if (folder / "session.json").is_file() else {}
    print(f"{folder.name}: {len(rows)} samples, {len(loads)} level loads, exit {meta.get('exit')}, {meta.get('env')}")
    print("| stay | level | seconds | private MiB low / high / last | working set MiB | GPU dedicated MiB | GPU shared MiB | handles | threads |")  # fmt: skip
    print("| --- | --- | --- | --- | --- | --- | --- | --- | --- |")
    bounds = [(0.0, "before the first level"), *loads]
    for index, (begin, level) in enumerate(bounds):
        end = bounds[index + 1][0] if index + 1 < len(bounds) else float("inf")
        inside = [r for r in rows if begin <= r["t"] < end]
        if not inside:
            continue

        print(
            f"| {index} | {level} | {inside[-1]['t'] - inside[0]['t']:.0f} | {span(inside, 'private_mb')} | "
            f"{span(inside, 'working_set_mb')} | {span(inside, 'gpu_dedicated_mb')} | {span(inside, 'gpu_shared_mb')} | {span(inside, 'handles')} | "
            f"{span(inside, 'threads')} |"
        )
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--summarise", type=Path, help="print the table of an earlier session folder and exit")
    ap.add_argument("--exe", type=Path)
    ap.add_argument("--out-root", type=Path, default=ROOT / "build" / "bench")
    ap.add_argument("--name", default="memory")
    ap.add_argument("--minutes", type=float, default=30.0)
    ap.add_argument("--every", type=float, default=10.0, help="seconds between samples")
    ap.add_argument("--level", help="start level (play_run --level)")
    ap.add_argument("--levelchange", help="DRAKAN_LEVELCHANGE value: level names, the first with @ticks")
    ap.add_argument("--env", action="append", default=[], help="KEY=VALUE for the game")
    a = ap.parse_args()
    if a.summarise:
        return report(a.summarise)
    if not a.exe:
        ap.error("--exe is required")
    folder = (a.out_root / a.name).resolve()
    folder.mkdir(parents=True, exist_ok=True)
    recipe = folder / "recipe.txt"
    recipe.write_text(f"clock milliseconds\n0 sync play 1\n{int(a.minutes * 60000)} quit\n", encoding="utf-8")
    timeout = a.minutes * 60 + 600
    env = ["DRAKAN_PLAYTEST_GOD=1", "DRAKAN_LIFECYCLE_LOG=1", *a.env]
    if a.levelchange:
        env.append(f"DRAKAN_LEVELCHANGE={a.levelchange}")
    cmd = [sys.executable, str(ROOT / "scripts" / "harness" / "play_run.py"), "--exe", str(a.exe), "--out-root",
           str(folder), "--name", "run", "--recipe", str(recipe), "--timeout", str(timeout), "--attempts", "1",
           "--timestamps"]  # fmt: skip
    if a.level:
        cmd += ["--level", a.level]
    for kv in env:
        cmd += ["--env", kv]
    bench.take_lock()
    stop = threading.Event()
    sampler = threading.Thread(target=sample, args=(a.exe, folder / "memory.csv", a.every, stop), daemon=True)
    try:
        sampler.start()
        code = subprocess.run(cmd, capture_output=True, text=True).returncode
    finally:
        stop.set()
        sampler.join()
        bench.release_lock()
    (folder / "session.json").write_text(json.dumps({"exit": code, "env": env, "minutes": a.minutes}, indent=1))
    return report(folder)


if __name__ == "__main__":
    sys.exit(main())
