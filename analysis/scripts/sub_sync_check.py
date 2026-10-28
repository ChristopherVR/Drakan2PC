"""Find where the decoded narration track starts inside an audio dump (DRAKAN_AUDIO_DUMP wav) by envelope cross-correlation.
usage: python -I sub_sync_check.py <dump.wav> <track.wav> <search_from_s> <search_to_s>
(track.wav = sub_adpcm_wav.py output, e.g. build/research/intro_narration.wav). Prints the dump time where track time 0 sits."""

import sys
import wave

import numpy as np


def load(p):
    w = wave.open(p)
    sr = w.getframerate()
    n = w.getnframes()
    ch = w.getnchannels()
    a = np.frombuffer(w.readframes(n), "<i2").astype(np.float32).reshape(-1, ch).mean(axis=1) / 32768
    return a, sr


def env(x, sr, hz=100):
    n = int(sr / hz)
    m = len(x) // n
    return np.sqrt((x[: m * n].reshape(m, n) ** 2).mean(axis=1))


d, sd = load(sys.argv[1])
r, sr = load(sys.argv[2])
lo, hi = float(sys.argv[3]), float(sys.argv[4])
ed, er = env(d, sd), env(r, sr)
ref = er[:6000]
ref = (ref - ref.mean()) / ref.std()
best = None
for off in range(int(lo * 100), int(hi * 100)):
    seg = ed[off : off + len(ref)]
    if len(seg) < len(ref):
        break
    s = (seg - seg.mean()) / (seg.std() + 1e-9)
    c = float((s * ref).mean())
    if best is None or c > best[0]:
        best = (c, off)
print("track t=0 sits at dump %.2f s (correlation %.2f)" % (best[1] / 100, best[0]))
