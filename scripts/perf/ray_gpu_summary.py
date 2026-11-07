"""Summarise the WebGPU / DXR ray pass GPU timings of a game log (GSR_RAY_GPU_TIMING=1).

Reads `[ray-gpu]` lines (one per timed pass: GPU milliseconds per dispatch or stage, the scene facts and, on WebGPU,
the CPU stages of the pass) and the `[perf N] flips/s=` lines of PS2X_PERF=1. Prints the median and 90th percentile
of every stage as JSON. With --budget STAGE=MS (repeatable) it exits 1 when a stage's median exceeds its budget, so a
recorded run can be checked against a performance budget.
"""

from __future__ import annotations

import argparse
import json
import re
import statistics
import sys
from pathlib import Path

RAY_LINE = re.compile(r"\[ray-gpu\] (?P<body>.*)")
STAGE = re.compile(r"(?P<name>[a-z_]+)=(?P<value>-?[0-9]+(?:\.[0-9]+)?)ms\b")
FACT = re.compile(r"(?P<name>[a-z_]+)=(?P<value>[0-9]+(?:x[0-9]+)?)(?=\s|$)")
CPU_STAGE = re.compile(r"(?P<name>[a-z]+)=(?P<value>[0-9]+(?:\.[0-9]+)?)")
PERF_LINE = re.compile(r"\[perf \d+\] flips/s=(?P<flips>[0-9.]+)")


def parse(text: str) -> dict:
    passes, flips = [], []
    for line in text.splitlines():
        match = RAY_LINE.search(line)
        if match:
            body = match.group("body")
            gpu_part, _, cpu_part = body.partition(" cpu:")
            entry = {"stages": {}, "facts": {}, "cpu": {}}
            for stage in STAGE.finditer(gpu_part):
                entry["stages"][stage["name"]] = float(stage["value"])
            for fact in FACT.finditer(gpu_part):
                if fact["name"] not in entry["stages"]:
                    entry["facts"][fact["name"]] = fact["value"]
            for stage in CPU_STAGE.finditer(cpu_part):
                entry["cpu"][stage["name"]] = float(stage["value"])
            if entry["stages"]:
                passes.append(entry)
            continue
        match = PERF_LINE.search(line)
        if match:
            flips.append(float(match["flips"]))
    return {"passes": passes, "flips": flips}


def percentile(values: list[float], fraction: float) -> float:
    ordered = sorted(values)
    return ordered[min(len(ordered) - 1, int(fraction * len(ordered)))]


def summarise(parsed: dict, skip: int = 0) -> dict:
    passes = parsed["passes"][skip:]
    stages: dict[str, list[float]] = {}
    cpu: dict[str, list[float]] = {}
    for entry in passes:
        for name, value in entry["stages"].items():
            stages.setdefault(name, []).append(value)
        for name, value in entry["cpu"].items():
            cpu.setdefault(name, []).append(value)

    def describe(values: list[float]) -> dict:
        return {"median": round(statistics.median(values), 3), "p90": round(percentile(values, 0.9), 3)}

    facts = passes[-1]["facts"] if passes else {}
    moving = [f for f in parsed["flips"] if f > 0]
    return {
        "passes": len(passes),
        "gpu_ms": {name: describe(values) for name, values in stages.items()},
        "cpu_ms": {name: describe(values) for name, values in cpu.items()},
        "last_facts": facts,
        "flips_per_second": describe(moving) if moving else None,
    }


def check(summary: dict, budgets: dict[str, float]) -> list[str]:
    failures = []
    for name, limit in budgets.items():
        stage = summary["gpu_ms"].get(name)
        if stage is None:
            failures.append(f"{name}: no samples")
        elif stage["median"] > limit:
            failures.append(f"{name}: median {stage['median']} ms > budget {limit} ms")
    return failures


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("log", type=Path, help="stdout.log of a run with GSR_RAY_GPU_TIMING=1")
    parser.add_argument("--skip", type=int, default=30, help="ignore the first N timed passes (level start)")
    parser.add_argument("--budget", action="append", default=[], metavar="STAGE=MS")
    args = parser.parse_args(argv)
    budgets = {}
    for item in args.budget:
        name, _, value = item.partition("=")
        try:
            budgets[name] = float(value)
        except ValueError:
            parser.error(f"invalid budget {item!r}")
    summary = summarise(parse(args.log.read_text(errors="replace")), args.skip)
    failures = check(summary, budgets)
    summary["budget_failures"] = failures
    print(json.dumps(summary, indent=2))
    if not summary["passes"]:
        print("no [ray-gpu] lines (run with GSR_RAY_GPU_TIMING=1)", file=sys.stderr)
        return 1
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
