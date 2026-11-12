"""Start-up, load and exit times of timestamped game runs.

    python scripts/harness/play_run.py --name load1 --timestamps --shots 0,100 \
        --env DRAKAN_IO_STATS=1 --env DRAKAN_LIFECYCLE_LOG=1
    python scripts/perf/load_phases.py build/harness/load1 [more run folders ...] [--json out.json]

Reads `stdout_<n>.log` as `play_run.py --timestamps` writes it (`[+seconds] line`, seconds since the process was
started). One run prints its phases; several runs print the median, the lowest and the highest of each phase, so runs of
two builds can be made alternately and compared (`--label` names a group: `--label old run1 run3 --label new run2
run4`).

Phases (seconds, wall clock):

  start-up              process start to the first guest GUI screen; with its parts: renderer ready, mod packs
                        (`[mods] disc profile` to `[mods] N pack(s) active`), services ready (`[drakan] HLE:`)
  loading screen N      every stay on the loading screen (GUI screen 14): level load, level change, loading a save
  level load N          `beforeLevelLoad` to `afterLevelLoad` of the engine's level loader (`DRAKAN_LIFECYCLE_LOG=1`)
  save write / restore  the same for a save and for a save restore
  to control            process start to `[in] autostart: player under control`; includes the menus, which the
                        harness taps through every 3 s, so it compares runs of one recipe, not what a player waits
  exit                  `[in] script quit` to the end of the process

With `DRAKAN_IO_STATS=1` the `[io]` lines (one per second with activity) are added up per loading screen and for the
whole run: disc reads and bytes, resource batches, requests and bytes. Those counts do not depend on the machine load;
the times do, so state the load with every time.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

STAMP = re.compile(r"^\[\+(\d+\.\d+)\] (.*)")
SCREEN = re.compile(r"\[in\] t=[\d.]+ frame \d+: gui screen (-?\d+) -> (-?\d+)")
LIFECYCLE = re.compile(r"\[lifecycle [\d.]+\] (before|after)(LevelLoad|SaveWrite|SaveRestore) level='([^']*)'")
MODS_DONE = re.compile(r"\[mods\] \d+ pack\(s\) active")
INSTALL = re.compile(r"mods: pack files installed in (\d+) ms")
LOADING_SCREEN = 14


def find_log(path: Path) -> Path:
    if path.is_file():
        return path
    logs = sorted(path.glob("stdout_*.log"))
    if not logs:
        raise SystemExit(f"{path}: no stdout_<n>.log (run play_run.py with --timestamps)")
    return logs[-1]


def io_counts(text: str) -> dict[str, int]:
    """The counters of one `[io]` line; `bytes` appears for the disc and for the resource service."""
    disc, _, rest = text.partition("; file calls=")
    out: dict[str, int] = {}
    m = re.search(r"disc reads=(\d+) bytes=(\d+)", disc)
    if m:
        out["disc_reads"], out["disc_bytes"] = int(m.group(1)), int(m.group(2))
    m = re.search(r"resource batches=(\d+) requests=(\d+) bytes=(\d+) overridden=(\d+)", rest)
    if m:
        out["batches"], out["requests"] = int(m.group(1)), int(m.group(2))
        out["resource_bytes"], out["overridden"] = int(m.group(3)), int(m.group(4))
    return out


def phases(path: Path) -> dict:
    lines: list[tuple[float, str]] = []
    for raw in path.read_text(encoding="latin-1").splitlines():
        m = STAMP.match(raw)
        if m:
            lines.append((float(m.group(1)), m.group(2)))
    if not lines:
        raise SystemExit(f"{path}: no `[+seconds]` lines (run play_run.py with --timestamps)")
    out: dict[str, float] = {}
    counts: dict[str, dict[str, int]] = {}
    io: list[tuple[float, dict[str, int]]] = []
    first: dict[str, float] = {}
    loads: list[tuple[float, float]] = []
    load_start = None
    open_events: dict[str, float] = {}
    event_index: dict[str, int] = {}
    for t, text in lines:

        def mark(name: str, when: float = t) -> None:
            first.setdefault(name, when)

        if "[gs] backend:" in text:
            mark("renderer")
        elif "[mods] disc profile" in text:
            mark("mods_begin")
        elif MODS_DONE.search(text):
            mark("mods_end")
        elif "[drakan] HLE:" in text:
            mark("services")
        elif "[in] autostart: player under control" in text:
            mark("control")
        elif "[in] script quit" in text:
            mark("quit")
        elif "[play_run] process exit" in text:
            mark("exit")
        elif text.startswith("[io] "):
            io.append((t, io_counts(text)))
        if m := INSTALL.search(text):
            out["mods: pack files installed (logged by the game)"] = int(m.group(1)) / 1000.0
        if m := SCREEN.search(text):
            mark("first_screen")
            before, after = int(m.group(1)), int(m.group(2))
            if after == LOADING_SCREEN:
                load_start = t
            elif before == LOADING_SCREEN and load_start is not None:
                loads.append((load_start, t))
                load_start = None
        if m := LIFECYCLE.search(text):
            kind = {"LevelLoad": "level load", "SaveWrite": "save write", "SaveRestore": "save restore"}[m.group(2)]
            if m.group(1) == "before":
                open_events[kind] = t
            elif kind in open_events:
                event_index[kind] = event_index.get(kind, 0) + 1
                out[f"{kind} {event_index[kind]} ({m.group(3)})"] = t - open_events.pop(kind)

    def span(name: str, a: str, b: str) -> None:
        if a in first and b in first:
            out[name] = first[b] - first[a]

    if "first_screen" in first:
        out["start-up (process start to first guest screen)"] = first["first_screen"]
    if "renderer" in first:
        out["start-up: renderer ready"] = first["renderer"]
    span("start-up: mod packs", "mods_begin", "mods_end")
    span("start-up: mod packs to services ready", "mods_end", "services")
    span("start-up: services ready to first guest screen", "services", "first_screen")
    for i, (a, b) in enumerate(loads, 1):
        name = f"loading screen {i}"
        out[name] = b - a
        total: dict[str, int] = {}
        for t, c in io:
            if a <= t <= b + 1.0:  # an `[io]` line reports the second before it
                for k, v in c.items():
                    total[k] = total.get(k, 0) + v
        if total:
            counts[name] = total
    if "control" in first:
        out["to control (includes the harness's menu taps)"] = first["control"]
    span("exit (script quit to process end)", "quit", "exit")
    if io:
        total = {}
        for _, c in io:
            for k, v in c.items():
                total[k] = total.get(k, 0) + v
        counts["whole run"] = total
    return {"log": str(path), "seconds": out, "counts": counts}


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("runs", nargs="*", type=Path, help="run folders (or logs) of the default group")
    ap.add_argument("--label", action="append", nargs="+", default=[], metavar=("NAME", "RUN"), help="a named group")
    ap.add_argument("--json", type=Path)
    a = ap.parse_args()
    groups: list[tuple[str, list[Path]]] = []
    if a.runs:
        groups.append(("runs", a.runs))
    for spec in a.label:
        groups.append((spec[0], [Path(p) for p in spec[1:]]))
    if not groups or any(not runs for _, runs in groups):
        ap.error("give at least one run folder (and at least one per --label)")
    report = {name: [phases(find_log(p)) for p in runs] for name, runs in groups}
    if a.json:
        a.json.write_text(json.dumps(report, indent=1), encoding="utf-8")
    names: list[str] = []
    for runs in report.values():
        for r in runs:
            names += [n for n in r["seconds"] if n not in names]
    width = max(len(n) for n in names) + 2
    header = "phase".ljust(width) + "".join(f"{g + ' (n=' + str(len(r)) + ')':>30}" for g, r in report.items())
    print(header)
    print("seconds: median [lowest .. highest]" if any(len(r) > 1 for r in report.values()) else "seconds")
    for n in names:
        row = n.ljust(width)
        for runs in report.values():
            values = [r["seconds"][n] for r in runs if n in r["seconds"]]
            if not values:
                cell = "-"
            elif len(values) == 1:
                cell = f"{values[0]:.3f}"
            else:
                cell = f"{statistics.median(values):.3f} [{min(values):.3f} .. {max(values):.3f}]"
            row += f"{cell:>30}"
        print(row)
    for group, runs in report.items():
        for r in runs:
            for name, c in r["counts"].items():
                mib = 1024.0 * 1024.0
                print(
                    f"{group}: {Path(r['log']).parent.name}: {name}: disc reads {c.get('disc_reads', 0)} "
                    f"({c.get('disc_bytes', 0) / mib:.1f} MiB), resource batches {c.get('batches', 0)}, requests "
                    f"{c.get('requests', 0)} ({c.get('resource_bytes', 0) / mib:.1f} MiB), overridden "
                    f"{c.get('overridden', 0)}"
                )
    return 0


if __name__ == "__main__":
    sys.exit(main())
