import json
import sys
import wave

import numpy as np
from faster_whisper import WhisperModel

w = wave.open(sys.argv[1])
sr = w.getframerate()
n = w.getnframes()
ch = w.getnchannels()
a = np.frombuffer(w.readframes(n), "<i2").astype(np.float32).reshape(-1, ch).mean(axis=1) / 32768
t0 = float(sys.argv[2])
t1 = float(sys.argv[3])
a = a[int(t0 * sr) : int(t1 * sr)]
m = int(len(a) * 16000 / sr)
a = np.interp(np.linspace(0, len(a) - 1, m), np.arange(len(a)), a).astype(np.float32)
model = WhisperModel("small.en", device="cpu", compute_type="int8")
segs, _ = model.transcribe(a, language="en", beam_size=5, word_timestamps=True, condition_on_previous_text=False)
out = []
for s in segs:
    for x in s.words:
        out.append((x.word, x.start + t0, x.end + t0))
json.dump(out, open(sys.argv[4], "w"))
print(len(out))
