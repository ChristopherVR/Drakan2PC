"""Correlate wall-clock RIP samples with independently measured thread CPU time.

RIP samples include blocked threads. They are NOT CPU-time samples and must not
be used to rank CPU consumption or infer a percentage of frame time.
"""

import argparse
import bisect
import collections
import csv
import datetime as dt
import json
import re


def timestamp(value):
    return dt.datetime.fromisoformat(value.replace("Z", "+00:00")).timestamp()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("samples")
    parser.add_argument("map")
    parser.add_argument("cpu", help="per-thread cumulative CPU snapshots JSON")
    parser.add_argument("--start", required=True, help="UTC start of analysis window")
    parser.add_argument("--seconds", type=float, default=18)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    start = timestamp(args.start)
    end = start + args.seconds
    points = collections.defaultdict(list)
    with open(args.cpu, encoding="utf-8-sig") as handle:
        for row in json.load(handle):
            t = timestamp(row["utc"])
            if start <= t <= end:
                points[int(row["tid"])].append((t, float(row["cpuMs"])))
    symbols = []
    base = None
    pattern = re.compile(r"^\s*([\da-fA-F]{4}):([\da-fA-F]{8})\s+(\S+)\s+([\da-fA-F]{16})")
    with open(args.map, errors="replace") as handle:
        for line in handle:
            if "Preferred load address is" in line:
                base = int(line.split("is")[1].split()[0], 16)
            match = pattern.match(line)
            if match:
                symbols.append((int(match[4], 16), match[3]))
    if base is None or not symbols:
        raise ValueError("No linker symbols or preferred image base")
    symbols = sorted(set(symbols))
    addresses = [entry[0] for entry in symbols]

    def resolve(value):
        index = bisect.bisect_right(addresses, base + int(value, 16)) - 1
        return symbols[index][1] if index >= 0 else "?"

    states = collections.defaultdict(collections.Counter)
    callers = collections.defaultdict(collections.Counter)
    with open(args.samples, newline="") as handle:
        for row in csv.DictReader(handle):
            tid, count = int(row["tid"]), int(row["count"])
            symbol = resolve(row["rva"]) if row["module"].lower().startswith("drakan") else row["module"]
            states[tid][symbol] += count
            if row.get("caller") and int(row["caller"], 16):
                callers[tid][resolve(row["caller"])] += count
    result = []
    for tid, values in points.items():
        values.sort()
        if len(values) < 2:
            continue
        elapsed = values[-1][0] - values[0][0]
        cpu_ms = values[-1][1] - values[0][1]
        if elapsed <= 0 or cpu_ms < 0:
            raise ValueError(f"Invalid CPU counters for thread {tid}")
        result.append(
            dict(
                tid=tid,
                observed_seconds=elapsed,
                cpu_ms=cpu_ms,
                cores=cpu_ms / (elapsed * 1000),
                wall_sample_states=states[tid].most_common(10),
                external_sample_callers=callers[tid].most_common(5),
            )
        )
    result.sort(key=lambda row: row["cores"], reverse=True)
    if not result:
        raise ValueError("No CPU snapshots inside requested window")
    with open(args.output, "w") as handle:
        json.dump(
            dict(
                window_start=args.start,
                requested_seconds=args.seconds,
                caveat="Wall RIP samples include blocked time; CPU counters rank threads, not functions. Window starts at process start plus configured delay, which approximates sampler initialization.",
                threads=result,
            ),
            handle,
            indent=2,
        )
    for row in result[:10]:
        print(
            f"thread {row['tid']}: {row['cores']:.3f} cores, {row['cpu_ms']:.1f} ms CPU / {row['observed_seconds']:.2f} s"
        )
        for name, count in row["wall_sample_states"][:4]:
            print(f"  {count:6d} wall samples: {name}")


if __name__ == "__main__":
    main()
