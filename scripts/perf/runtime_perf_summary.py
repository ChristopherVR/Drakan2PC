"""Summarize existing performance logs; never launches the game.

Selects the last N windows with worldUpdates >= 20 (explicitly a heuristic).
Reports elapsed-weighted rates and per-world bridge waits, not scanout FPS.
Backend profiles are summarized separately; their windows are not aligned.
"""

import argparse
import json
import re
from pathlib import Path


def summarize(directory, tail):
    text = (directory / "stderr.log").read_text(encoding="utf-8", errors="replace")
    rows = []
    for line in text.splitlines():
        match = re.search(r"\[gsr-pacing\] ([\d.]+)s:", line)
        if match:
            row = {key: float(value) for key, value in re.findall(r"(\w+)=([\d.]+)", line)}
            row["seconds"] = float(match[1])
            if row.get("worldUpdates", 0) >= 20:
                rows.append(row)
    selected = rows[-tail:]
    seconds = sum(row["seconds"] for row in selected)
    worlds = sum(row["seconds"] * row["worldUpdates"] for row in selected)
    result = {
        "run": directory.name,
        "eligible_windows": len(rows),
        "selected_windows": len(selected),
        "seconds": seconds,
        "worlds_estimated_from_rounded_rates": worlds,
    }
    if seconds and worlds:
        result["rates"] = {
            key: sum(row["seconds"] * row[key] for row in selected) / seconds
            for key in ("worldUpdates", "worldPresents", "guestVsyncs", "hostTicks", "presentCalls")
            if all(key in row for row in selected)
        }
        result["ms_per_world"] = {
            key: sum(row[key] for row in selected) / worlds
            for key in ("lockMs", "presentMs", "drainMs", "handoffMs", "fairMs")
            if all(key in row for row in selected)
        }
        result["world_interval_ms"] = 1000 * seconds / worlds
    manifest = directory / "manifest.json"
    if manifest.exists():
        metadata = json.loads(manifest.read_text(encoding="utf-8-sig"))
        result["identity"] = {key: metadata.get(key) for key in ("sha256", "script_sha256", "input_ini_sha256")}
        result["settings"] = {
            key: value
            for key, value in metadata.get("environment", {}).items()
            if key
            in (
                "DRAKAN_WORLD_RATE",
                "DRAKAN_DLSS",
                "DRAKAN_DLSS_NEURAL",
                "PS2X_GS_SCALE",
                "DRAKAN_WINDOW_WIDTH",
                "DRAKAN_WINDOW_HEIGHT",
                "DRAKAN_AUDIO",
                "DRAKAN_VSYNC",
                "DRAKAN_FRAME_LIMIT",
                "GSR_PROFILE",
                "DRAKAN_RUNTIME_PROFILE",
            )
        }
    profiles = []
    for line in text.splitlines():
        if "[gsr-profile]" in line or "[runtime-profile]" in line:
            buckets = {}
            for key, values in re.findall(r"(\w+)=(\d+/[\d./]+)", line):
                numbers = list(map(float, values.split("/")))
                buckets[key] = {"calls": numbers[0], "inclusive_ms": numbers[1]}
                if len(numbers) == 4:
                    buckets[key].update(exclusive_ms=numbers[2], max_ms=numbers[3])
            profiles.append(buckets)
    result["last_profile_windows_unaligned"] = profiles[-tail:]
    result["limitations"] = [
        "Tail selection can include transitions; inspect selected scene and input timeline.",
        "Waits overlap work on other threads and must not be summed as CPU usage.",
        "Different executable hashes, scripts, or render scales prevent causal A/B attribution.",
    ]
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directories", nargs="+", type=Path)
    parser.add_argument("--tail", type=int, default=5)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    if args.tail < 1:
        parser.error("--tail must be positive")
    output = json.dumps([summarize(path, args.tail) for path in args.directories], indent=2) + "\n"
    if args.output:
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output, end="")
