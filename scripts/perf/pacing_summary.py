"""Read private pacing logs without launching the game or printing private paths."""

import argparse
import json
import re
import statistics
from pathlib import Path


def summarize(path):
    text = path.read_text(encoding="utf-8", errors="replace")
    # Older logs mislabeled ALL host ticks as presents, including skipped NR
    # ticks. Retain that distinction in any comparison with the new counters.
    old = re.findall(r"host presents ([\d.]+)/s .*?guest vsyncs ([\d.]+)/s", text)
    new = [
        dict((key, float(value)) for key, value in re.findall(r"(\w+)=([\d.]+)", line.split("[gsr-pacing]", 1)[1]))
        for line in text.splitlines()
        if "[gsr-pacing]" in line and "hostTicks=" in line
    ]
    result = {"old_windows": len(old), "new_windows": len(new)}
    if old:
        result["old_host_tick_rate_median"] = statistics.median(float(x[0]) for x in old)
        result["old_guest_vsync_rate_median"] = statistics.median(float(x[1]) for x in old)
        result["limitation"] = "Old host presents field counts ticks, not unique worlds or confirmed display frames"
    if new:
        result["new_medians"] = {key: statistics.median(row[key] for row in new if key in row) for key in new[0]}
        gameplay = [row for row in new if row.get("worldUpdates", 0) > 0]
        result["gameplay_windows"] = len(gameplay)
        if gameplay:
            result["gameplay_medians"] = {
                key: statistics.median(row[key] for row in gameplay if key in row) for key in gameplay[0]
            }
    slow = re.findall(r"slow host tick: wait ([\d.]+) ms, present ([\d.]+) ms", text)
    result["slow_host_ticks"] = len(slow)
    if slow:
        result["max_old_present_plus_lock_ms"] = max(float(x[1]) for x in slow)
    result["scene_sync_windows"] = text.count("[scene-pacing]")
    result["neural_handoff_timeouts"] = text.count("host handoff timed out")
    result["capture_indicators"] = len(re.findall(r"screenshot .*?->|windowScreenshot=", text))
    return result


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("logs", nargs="+", type=Path)
    args = parser.parse_args()
    for index, path in enumerate(args.logs):
        print(json.dumps({"input": index, **summarize(path)}, sort_keys=True))
