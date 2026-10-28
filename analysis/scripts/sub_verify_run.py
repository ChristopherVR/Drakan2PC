"""Compares the cue times logged by a run (DRAKAN_SUB_LOG=1) with Whisper word times of that run's audio dump.
usage: python -I sub_verify_run.py <run.log> <dump_words.json>
(dump_words.json: <venv>/python sub_whisper_dump.py <dump.wav> <from_s> <to_s> <out.json>.)
Prints, per cue, how long before (-) / after (+) its first word was heard the cue appeared, the on-screen gaps between cues and their durations."""

import json
import re
import statistics
import sys

words = json.load(open(sys.argv[2]))
cues, cur = [], None
for l in open(sys.argv[1], errors="replace").read().replace("\r", "\n").split("\n"):
    m = re.match(r"\[subtitles\] cue (on|off)\s+at audio time ([0-9.]+) s \(track time ([0-9.]+) s\): ?(.*)", l)
    if m:
        if m.group(1) == "on":
            cur = [float(m.group(2)), None, m.group(4)]
        elif cur:
            cur[1] = float(m.group(2))
            cues.append(cur)
            cur = None
norm = lambda w: re.sub(r"[^a-z]", "", w.lower())
dw = [(norm(w), a, b) for w, a, b in words if norm(w)]
d = []
for on, off, text in cues:
    toks = [norm(x) for x in text.split() if norm(x)]
    cand = sorted((abs(a - (on + 0.08)), a) for w, a, b in dw if toks and w == toks[0] and abs(a - (on + 0.08)) < 3.0)
    if cand:
        d.append(on - cand[0][1])
print(
    "%d cues; cue vs first heard word: median %+.3f s, 25-75%% %+.3f .. %+.3f s (Whisper word starts are +-0.1 s)"
    % (len(cues), statistics.median(d), sorted(d)[len(d) // 4], sorted(d)[3 * len(d) // 4])
)
gaps = [cues[i + 1][0] - cues[i][1] for i in range(len(cues) - 1)]
durs = [c[1] - c[0] for c in cues]
print(
    "on-screen gaps between cues: max %.2f s, > 0.5 s: %d of %d; cue duration min %.2f max %.2f s"
    % (max(gaps), sum(g > 0.5 for g in gaps), len(gaps), min(durs), max(durs))
)
