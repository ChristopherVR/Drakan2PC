"""Word-level Whisper timestamps of the decoded narration tracks (build/research/intro_narration.wav, outro_narration.wav) -> <outdir>/<name>_words.json.
usage: <venv>/python sub_whisper_words.py <outdir>   (pip install faster-whisper numpy; CPU, small.en model downloads on first use)"""

import json
import os
import sys
import wave

import numpy as np
from faster_whisper import WhisperModel

m = WhisperModel("small.en", device="cpu", compute_type="int8")
for name in ("intro", "outro"):
    w = wave.open(
        os.path.join(
            os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "research", "%s_narration.wav" % name
        )
    )
    a = np.frombuffer(w.readframes(w.getnframes()), "<i2").astype(np.float32) / 32768
    sr = w.getframerate()
    n = int(len(a) * 16000 / sr)
    a = np.interp(np.linspace(0, len(a) - 1, n), np.arange(len(a)), a).astype(np.float32)
    segs, _ = m.transcribe(a, language="en", beam_size=5, word_timestamps=True, condition_on_previous_text=False)
    out = []
    for s in segs:
        out.append({"s": s.start, "e": s.end, "t": s.text, "w": [(x.word, x.start, x.end) for x in s.words]})
    json.dump(out, open(r"%s/%s_words.json" % (sys.argv[1], name), "w"))
    print(name, len(out))
