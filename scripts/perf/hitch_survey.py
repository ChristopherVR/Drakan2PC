"""Hitch survey of a `DRAKAN_FRAME_AUDIT=2` run: every long world frame of free gameplay with what the log shows for it.

    python scripts/harness/play_run.py --name walk --timestamps --timeout 400 \
        --recipe scripts/harness/recipes/perf_walk_surdana.txt \
        --env DRAKAN_FRAME_AUDIT=2 --env DRAKAN_PLAYTEST_GOD=1 --env GSR_TEXUP_LOG=1
    python scripts/perf/hitch_survey.py build/harness/walk [--json out.json] [--list 40]

A frame is the time between two EE display flips (`[ft] F`, microseconds on the steady clock). Only flips of free
gameplay count: after `[in] autostart: player under control` and on the world screen (-1; a level change shows the
loading screen 14). Frames that contain a scripted screenshot or memory dump are left out, and the frames of a scripted
teleport (`place` until the playtest hold releases the player, while the new surroundings stream in) are reported apart.

For every frame over 2x and over 4x the median the report gives the cause it can see in the log:

  gs <ms>        GS worker records over 8 ms inside the frame (`[ft] W`: one renderer record, type and duration)
  present <ms>   host present ticks over 8 ms inside the frame (`[ft] H`, lock / present split)
  res <ms>       resource batches of the HLE file service inside the frame (`[ft] R`: duration, requests, bytes, file)
  vblank-late    a guest vblank delivered more than 8 ms late (`[ft] V`); a symptom, listed after the others
  log: ...       the other log lines printed during the frame (texture jobs, late shader work, file opens, ...)

A frame with none of these is listed as `unattributed`; `DRAKAN_FRAME_AUDIT=3` and `scripts/perf/ee_samples.py` show
what the EE thread ran in such a gap. The numbers depend on the machine load of the moment: compare runs made
alternately, and compare causes and counts rather than single frames.
"""

from __future__ import annotations

import argparse
import itertools
import json
import re
import statistics
import sys
from pathlib import Path

STAMP = re.compile(r"^\[\+(\d+\.\d+)\] ")
FT = re.compile(r"\[ft\] ([A-Z]) (-?\d+)(.*)")
SCREEN = re.compile(r"\[in\] t=[\d.]+ frame \d+: gui screen (-?\d+) -> (-?\d+) \(gameplay=(\d)\)")
SCRIPTED = ("[in] screenshot", "requested window screenshot", "[in] dump")
NOISE = ("[ft] ", "[frame-uncapped]", "[perf ", "[runtime-profile]", "[gsr-pacing]", "[gsr-presentation]", "[io] ")


def find_log(path: Path) -> Path:
    if path.is_file():
        return path
    logs = sorted(path.glob("stdout_*.log"))
    if logs:
        return logs[-1]
    if (path / "drakan.log").is_file():
        return path / "drakan.log"
    raise SystemExit(f"{path}: no stdout_<n>.log or drakan.log")


def parse(path: Path) -> dict:
    """Flips with their gameplay state, the timed events and the plain log lines, all in file order."""
    flips: list[tuple[int, int]] = []  # (us, 0 = not free gameplay, 1 = free gameplay, 2 = inside a scripted teleport)
    events: list[tuple[int, int, str]] = []  # (start us, duration us, description)
    notes: list[tuple[int, str]] = []  # (us of the last [ft] line before it, text)
    scripted: list[int] = []
    control = False
    screen = None
    teleport = False
    last_us = 0
    for raw in path.read_text(encoding="latin-1").splitlines():
        line = STAMP.sub("", raw)
        m = FT.search(line)
        if m:
            kind, us, rest = m.group(1), int(m.group(2)), m.group(3).split()
            last_us = max(last_us, us)
            if kind == "F":
                flips.append((us, (2 if teleport else 1) if control and screen == -1 else 0))
            elif kind == "W" and len(rest) >= 2:
                events.append((us, int(rest[1]), f"gs record type {rest[0]}"))
            elif kind == "H":
                took = sum(int(v) for v in re.findall(r"=(-?\d+)", m.group(3)))
                events.append((us, took, "present " + " ".join(rest)))
            elif kind == "R" and len(rest) >= 3:
                events.append((us, int(rest[0]), f"res {rest[1]} requests {rest[2]} bytes {' '.join(rest[3:])}"))
            elif kind == "V" and rest and int(rest[0]) > 8000:
                events.append((us - int(rest[0]), int(rest[0]), "vblank-late"))
            continue
        s = SCREEN.search(line)
        if s:
            screen = int(s.group(2))
            continue
        if "[in] autostart: player under control" in line:
            control = True
            if screen is None:
                screen = -1
            continue
        if "[playtest] script place" in line:
            teleport = True
        elif "[playtest] released" in line or "[playtest] gave up" in line:
            teleport = False
        if any(tag in line for tag in SCRIPTED):
            scripted.append(last_us)
        if line.strip() and not any(tag in line for tag in NOISE):
            notes.append((last_us, line.strip()[:160]))
    return {"flips": flips, "events": events, "notes": notes, "scripted": scripted}


def survey(data: dict, list_limit: int) -> dict:
    flips = data["flips"]
    frames = []  # (start us, end us)
    teleport_ms = []
    for (a, play_a), (b, play_b) in itertools.pairwise(flips):
        if play_a == 1 and play_b == 1 and b > a:
            frames.append((a, b))
        elif play_a and play_b and b > a:
            teleport_ms.append((b - a) / 1000.0)
    if len(frames) < 20:
        return {
            "frames": len(frames),
            "error": "fewer than 20 gameplay frames with [ft] F lines (DRAKAN_FRAME_AUDIT=2?)",
        }
    # A scripted action lands in the frame that contains it or, logged late, in the next one.
    scripted = sorted(data["scripted"])

    def is_scripted(a: int, b: int) -> bool:
        return any(a - 60000 <= s <= b for s in scripted)

    natural = [(a, b) for a, b in frames if not is_scripted(a, b)]
    durations = sorted((b - a) / 1000.0 for a, b in natural)
    median = statistics.median(durations)

    def pct(p: float) -> float:
        return durations[min(len(durations) - 1, int(p * len(durations)))]

    first = frames[0][0]
    long_frames = []
    for a, b in natural:
        ms = (b - a) / 1000.0
        if ms <= 2 * median:
            continue
        details = []
        buckets: dict[str, float] = {}
        for start, took, what in data["events"]:
            if start < b and start + took > a:
                key = what.split()[0]
                buckets[key] = buckets.get(key, 0.0) + (min(b, start + took) - max(a, start)) / 1000.0
                if key == "res":
                    details.append(what)
        # A late vblank is a symptom (the EE thread was held up), so it is named after the causes proper.
        ranked = sorted(buckets.items(), key=lambda kv: (kv[0] == "vblank-late", -kv[1]))
        causes = [f"{key} {total:.1f} ms" for key, total in ranked] + details
        lines = [text for us, text in data["notes"] if a <= us < b]
        long_frames.append(
            {
                "at_s": round((a - first) / 1e6, 3),
                "ms": round(ms, 2),
                "times_median": round(ms / median, 1),
                "causes": causes or ["unattributed"],
                "log": lines[:6],
            }
        )
    over2 = [f for f in long_frames if f["ms"] > 2 * median]
    over4 = [f for f in long_frames if f["ms"] > 4 * median]
    by_cause: dict[str, int] = {}
    for f in over2:
        key = f["causes"][0].split()[0]
        by_cause[key] = by_cause.get(key, 0) + 1
    seconds = sum(b - a for a, b in natural) / 1e6
    return {
        "frames": len(natural),
        "scripted_frames_excluded": len(frames) - len(natural),
        "gameplay_seconds": round(seconds, 1),
        "median_ms": round(median, 2),
        "p95_ms": round(pct(0.95), 2),
        "p99_ms": round(pct(0.99), 2),
        "max_ms": round(durations[-1], 2),
        "over_2x_median": len(over2),
        "over_4x_median": len(over4),
        "over_2x_per_minute": round(len(over2) * 60.0 / seconds, 1),
        "over_4x_per_minute": round(len(over4) * 60.0 / seconds, 1),
        "over_2x_by_first_cause": dict(sorted(by_cause.items(), key=lambda kv: -kv[1])),
        "teleport_frames": len(teleport_ms),
        "teleport_over_4x_median": sum(1 for ms in teleport_ms if ms > 4 * median),
        "teleport_max_ms": round(max(teleport_ms), 2) if teleport_ms else 0.0,
        "long_frames": sorted(long_frames, key=lambda f: -f["ms"])[:list_limit],
    }


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("run", type=Path, help="run folder of play_run.py, or a log file")
    ap.add_argument("--json", type=Path, help="also write the whole report as JSON")
    ap.add_argument("--list", type=int, default=25, help="how many long frames to list, longest first")
    a = ap.parse_args()
    report = survey(parse(find_log(a.run)), a.list)
    if a.json:
        a.json.write_text(json.dumps(report, indent=1), encoding="utf-8")
    if "error" in report:
        print(report["error"])
        return 1
    print(
        f"{report['frames']} gameplay frames over {report['gameplay_seconds']} s "
        f"({report['scripted_frames_excluded']} scripted frames excluded): median {report['median_ms']} ms, "
        f"p95 {report['p95_ms']}, p99 {report['p99_ms']}, max {report['max_ms']}"
    )
    print(
        f"over 2x median: {report['over_2x_median']} ({report['over_2x_per_minute']} per minute), "
        f"over 4x median: {report['over_4x_median']} ({report['over_4x_per_minute']} per minute)"
    )
    print("first cause of the frames over 2x:", report["over_2x_by_first_cause"])
    if report["teleport_frames"]:
        print(
            f"scripted teleports: {report['teleport_frames']} frames, {report['teleport_over_4x_median']} over 4x the "
            f"median, longest {report['teleport_max_ms']} ms"
        )
    for f in report["long_frames"]:
        print(f"  +{f['at_s']:8.3f} s  {f['ms']:7.2f} ms ({f['times_median']}x)  {'; '.join(f['causes'])}")
        for text in f["log"]:
            print(f"      log: {text}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
