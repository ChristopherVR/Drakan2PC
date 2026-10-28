"""RMS before / after every non-park key-on, measured in a DRAKAN_AUDIO_DUMP wav (key-on log line carries f = SPU2 frames = dump position).
usage: python -I snd_dump_check.py <run.log> <dump.wav>   (run with DRAKAN_SND_LOG=3)"""

import re
import sys
import wave

import numpy as np

log = sys.argv[1]
wav = sys.argv[2]
w = wave.open(wav)
sr = w.getframerate()
a = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32).reshape(-1, 2) / 32768


def rms(t0, t1):
    x = a[max(0, int(t0 * sr)) : int(t1 * sr)]
    return float(np.sqrt((x**2).mean())) if len(x) else 0.0


lines = open(log, errors="replace").read().replace("\r", "\n").split("\n")
rows = []
for l in lines:
    m = re.match(r"\[spu ([0-9.]+)\]   KON c(\d)\.v(\d+) ssa=([0-9a-f]+) .* f=(\d+)", l)
    if m and m.group(4) != "5000":
        t = int(m.group(5)) / 48000
        rows.append((t, m.group(2), m.group(3), m.group(4)))
for t, c, v, ssa in rows:
    print(
        "%7.2f c%s.v%-2s %-8s before(0.3s)=%.4f after(0.05..0.35s)=%.4f"
        % (t, c, v, ssa, rms(t - 0.3, t), rms(t + 0.05, t + 0.35))
    )
