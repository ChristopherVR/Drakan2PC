"""Summarise a mouse-look run recorded with DRAKAN_MOUSE_AUDIT=1 DRAKAN_FRAME_AUDIT=2 (scripts/harness/mouse_drive.py).

  python scripts/perf/mouse_audit_report.py build/harness/<run>/stdout.log [...]

The drive is a steady 2000 counts/s with direction changes every 3.5 s. Reported over the 1 .. 14 s after the drive started:
fps, present gap SD / p99 / max (ms), engine tick dt SD (ms), and `shown_sd`: how far (in ms of mouse motion, SD) the view
angle shown by each present is from a straight line in present time. Roughly 0 = every frame shows the motion it should,
2 ms and up on a 60 fps display is visible as microstutter; it comes from variation of the delay between the engine reading
the mouse and the frame reaching the display, not from the input path (see docs/journal/2026-10-31/mouse-microstutter.md).
"""

import re
import sys

import numpy as np


def run(f):
    raw = []
    P = []
    U = []
    S = []
    for l in open(f, errors="ignore"):
        m = re.match(r"\[mouse-raw\] t=(\d+)", l)
        if m:
            raw.append(int(m.group(1)))
            continue
        m = re.match(r"\[mouse-aud\] t=(\d+) dx=(-?\d+)", l)
        if m:
            S.append((int(m.group(1)), int(m.group(2))))
            continue
        m = re.match(r"\[ft\] P (\d+)", l)
        if m:
            P.append(int(m.group(1)))
            continue
        m = re.match(r"\[ft\] U (\d+) (\S+)", l)
        if m:
            U.append((int(m.group(1)), float(m.group(2))))
    raw = np.array(raw)
    i = np.where(np.diff(raw) > 5e6)[0]
    start = raw[i[-1] + 1]
    P = np.array([p for p in P if 1 <= (p - start) / 1e6 <= 14])
    g = np.diff(P) / 1000
    S = [s for s in S if 1 <= (s[0] - start) / 1e6 <= 14]
    Pa = np.array(sorted(P))
    # shown-vs-linear per constant-sign segment, only segments >=2.5 s
    segs = []
    cur = [S[0]]
    for x in S[1:]:
        if np.sign(x[1]) != np.sign(cur[-1][1]) and abs(x[1]) > 2:
            segs.append(cur)
            cur = [x]
        else:
            cur.append(x)
    segs.append(cur)
    res = []
    for s in segs:
        if len(s) < 100:
            continue
        s = s[20:-5]
        tb = np.array([a for a, _ in s])
        cum = np.cumsum([abs(d) for _, d in s])
        idx = np.searchsorted(Pa, tb)
        ok = idx < len(Pa)
        tp = Pa[idx[ok]]
        c = cum[ok]
        A = np.vstack([tp / 1e6, np.ones(len(tp))]).T
        k, b = np.linalg.lstsq(A, c, rcond=None)[0]
        res.append(((c - (k * tp / 1e6 + b)) / k * 1000).std())
    d = np.array([v for t, v in U if 1 <= (t - start) / 1e6 <= 14])
    return dict(
        fps=1000 / g.mean(),
        gap_sd=g.std(),
        gap_p99=np.percentile(g, 99),
        gap_max=g.max(),
        dt_sd=d.std(),
        shown_sd=np.mean(res) if res else float("nan"),
    )


for f in sys.argv[1:]:
    r = run(f)
    print(f.split("/")[-2], " ".join("%s=%.2f" % kv for kv in r.items()))
