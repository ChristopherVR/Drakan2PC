#!/usr/bin/env python3
"""Summarize existing stability logs without rerunning boots or rewriting them.

Run from any directory: python scripts/harness/stability/stab_audit.py [--output evidence.json]
Only completed boots (with a .rc file) count. The shell verdict remains the
runner's authority; this independent reader distinguishes coverage from exit.
"""

import argparse
import collections
import hashlib
import json
import pathlib
import re


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[3]
    logs = root / "build/recomp_stab/stab/logs"
    groups = collections.defaultdict(list)
    for rcfile in sorted(logs.glob("*.rc")):
        stem = rcfile.stem
        match = re.fullmatch(r"(.+)_([a-e])_(\d+)", stem)
        if not match:
            continue
        logfile, infile = logs / f"{stem}.log", logs / f"{stem}.in.log"
        raw = logfile.read_bytes() if logfile.exists() else b""
        text = raw.decode(errors="replace")
        inp = infile.read_text(errors="replace") if infile.exists() else ""
        rc = int(rcfile.read_text().strip())
        frame = re.search(r"\[gsr\] exit: frames=(\d+)", text)
        targets = re.findall(r"guest-branch:missing-target\][^\n]*?target=(0x[0-9a-fA-F]+)", text)
        quit = "script quit" in inp
        if not logfile.exists():
            verdict = "NOLOG"
        elif "[crash]" in text:
            verdict = "CRASH"
        elif re.search(r"Error during program execution|\[terminate\]|\[main\] fatal", text):
            verdict = "DEAD_EXC"
        elif re.search(r"trace-ring\] #", text):
            verdict = "DEAD"
        elif any(int(t, 16) >= 16 for t in targets):
            verdict = "MISSING"
        elif rc == 124:
            verdict = "HANG"
        elif rc:
            verdict = f"EXIT{rc}"
        elif frame is None:
            verdict = "NOEXIT"
        elif int(frame[1]) == 0:
            verdict = "NOPROGRESS"
        elif not quit:
            verdict = "INCOMPLETE"
        else:
            verdict = "OK"
        groups[match[1]].append(
            dict(
                boot=stem,
                verdict=verdict,
                frames=int(frame[1]) if frame else None,
                script_quit=quit,
                rc=rc,
                sha256=hashlib.sha256(raw).hexdigest(),
            )
        )
    result = {
        name: {
            "completed_boots": len(boots),
            "verdicts": dict(collections.Counter(b["verdict"] for b in boots)),
            "boots": boots,
        }
        for name, boots in groups.items()
    }
    if args.output:
        args.output.write_text(json.dumps(result, indent=2) + "\n")
    for name, item in result.items():
        frames = [b["frames"] for b in item["boots"] if b["frames"] is not None]
        print(
            f"{name}: boots={item['completed_boots']} {item['verdicts']} frames={min(frames) if frames else '?'}..{max(frames) if frames else '?'}"
        )


if __name__ == "__main__":
    main()
