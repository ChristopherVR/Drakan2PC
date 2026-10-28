#!/usr/bin/env python
"""Summarise [perf N] lines (PS2X_PERF=1) of one or more run logs: averages for the title menu and for Level 0.
Usage: perf_summary.py run1.log [run2.log ...]"""

import re
import statistics as st
import sys

rx = re.compile(
    r"\[perf (\d+)\] flips/s=([\d.]+) (?:flips2/s=([\d.]+) )?vblank/s=([\d.]+) gifpk/s=(\d+)(?: gifMB/s=[\d.]+)?(?: vu1Mcyc/s=([\d.]+) vu1runs/s=(\d+))?(?: vu1Mpairs/s=[\d.]+)? ee_thread_cpu=(\d+)% ee_ms/flip=([\d.]+) other_cpu=(\d+)%"
)


def parse(path):
    rows = []
    for l in open(path, errors="replace"):
        m = rx.search(l)
        if m:
            g = m.groups()
            rows.append(
                dict(
                    n=int(g[0]),
                    flips=float(g[1]),
                    flips2=float(g[2] or 0),
                    vblank=float(g[3]),
                    gifpk=int(g[4]),
                    vucyc=float(g[5] or 0),
                    vuruns=int(g[6] or 0),
                    cpu=int(g[7]),
                    msflip=float(g[8]),
                    other=int(g[9]),
                )
            )
    return rows


def avg(rows, key):
    return st.mean(r[key] for r in rows) if rows else float("nan")


for path in sys.argv[1:]:
    rows = parse(path)
    # menu: steady state with ~60 vblank/s and a live display; level 0: VU1 busy and the game running
    menu = [r for r in rows if r["vblank"] > 55 and r["gifpk"] < 2000 and r["n"] > 4 and r["flips2"] > 20]
    level = [r for r in rows if r["gifpk"] > 5000 and r["cpu"] > 50 and r["vblank"] > 3 and r["n"] > 18]
    dead = [r for r in rows if r["n"] > 25 and r["cpu"] == 0]
    print(f"{path}: {len(rows)} samples")
    if menu:
        print(
            f"  menu   : {avg(menu, 'vblank'):5.1f} vblank/s  {avg(menu, 'flips2'):5.1f} frames/s  EE-thread CPU {avg(menu, 'cpu'):3.0f}%  {avg(menu, 'msflip'):5.2f} ms/flip"
        )
    if level:
        print(
            f"  level 0: {avg(level, 'vblank'):5.1f} vblank/s  {avg(level, 'flips2'):5.1f} frames/s  EE-thread CPU {avg(level, 'cpu'):3.0f}%  {avg(level, 'msflip'):5.2f} ms/flip  VU1 {avg(level, 'vucyc'):5.1f} Mcyc/s  other threads {avg(level, 'other'):3.0f}%"
        )
    if dead:
        print(f"  (guest froze: {len(dead)} samples with 0% EE CPU)")
