"""Builds the shipped narration SRTs: aligns the proofread cue texts (local cue_text.json, "|" = line break inside a cue) to the Whisper word
timestamps (sub_whisper_words.py) and applies the timing rules: lead 80 ms, tail 300 ms, bridge gaps < 1.5 s (the cue stays until the next one starts),
minimum 1.2 s, never overlap. Cue time 0 = first sample of the soundtrack (the runtime starts the clock at the voice key-on).
usage: python -I sub_retime.py <dir with intro_words.json / outro_words.json>   -> prints the cues, writes intro_new.srt / outro_new.srt there"""

import difflib
import json
import re
import sys

S = sys.argv[1]
from pathlib import Path

# Transcript text stays beside locally generated word timestamps.
cue_text = json.loads((Path(S) / "cue_text.json").read_text(encoding="utf-8"))
INTRO = cue_text["intro"]
OUTRO = cue_text["outro"]


def norm(w):
    return re.sub(r"[^a-z]", "", w.lower().replace("’", "'").replace("'s", ""))


def sec(t):
    ms = round(t * 1000)
    return "%02d:%02d:%02d,%03d" % (ms // 3600000, ms // 60000 % 60, ms // 1000 % 60, ms % 1000)


def build(name, cues, dropafter=None):
    ws = []
    for s in json.load(open(f"{S}/{name}_words.json")):
        for w, a, b in s["w"]:
            if norm(w):
                ws.append((norm(w), a, b))
    if dropafter:
        ws = [w for w in ws if w[1] < dropafter]
    toks = []
    cid = []
    for i, c in enumerate(cues):
        for w in re.split(r"[ |]+", c):
            if norm(w):
                toks.append(norm(w))
                cid.append(i)
    sm = difflib.SequenceMatcher(None, toks, [w[0] for w in ws], autojunk=False)
    st = [None] * len(toks)
    en = [None] * len(toks)
    for a, b, n in sm.get_matching_blocks():
        for k in range(n):
            st[a + k] = ws[b + k][1]
            en[a + k] = ws[b + k][2]
    print(name, "matched %d/%d tokens" % (sum(x is not None for x in st), len(toks)))
    # interpolate
    for i in range(len(toks)):
        if st[i] is None:
            j = i
            while j < len(toks) and st[j] is None:
                j += 1
            lo = en[i - 1] if i > 0 else 0.0
            hi = st[j] if j < len(toks) else lo + 0.3 * (j - i)
            n = j - i
            for k in range(n):
                st[i + k] = lo + (hi - lo) * k / n
                en[i + k] = lo + (hi - lo) * (k + 1) / n
    raw = []
    for i, c in enumerate(cues):
        idx = [k for k in range(len(toks)) if cid[k] == i]
        raw.append([min(st[k] for k in idx), max(en[k] for k in idx), c.replace("|", "\n")])
    # timing rules
    LEAD = 0.08
    TAIL = 0.30
    GAP = 1.5
    MINDUR = 1.2
    out = []
    for i, (s, e, t) in enumerate(raw):
        s = max(0, s - LEAD)
        e = e + TAIL
        nxt = raw[i + 1][0] - LEAD if i + 1 < len(raw) else None
        words = len(t.split())
        need = max(MINDUR, words / 3.0 * 1.0 if False else MINDUR)
        if nxt is not None and nxt - e < GAP:
            e = max(e, nxt - 0.04)  # bridge short gaps (SRT cue back-to-back)
        if e - s < need:
            e = s + need
        if nxt is not None and e > nxt:
            e = nxt - 0.04
        out.append((s, e, t))
    return out


import textwrap


def reflow(t, maxw=44):
    """keeps the authored line breaks ('|') when every line is <= maxw + 2 characters, else wraps into balanced lines"""
    if max(len(x) for x in t.split(chr(10))) <= maxw + 2:
        return t
    flat = " ".join(t.split())
    n = -(-len(flat) // maxw)
    return chr(10).join(textwrap.wrap(flat, width=-(-len(flat) // n) + 4))


def write(path, cues):
    cues = [(a, b, reflow(t)) for a, b, t in cues]
    with open(path, "w", encoding="utf-8", newline="\r\n") as f:
        for i, (s, e, t) in enumerate(cues, 1):
            f.write(f"{i}\n{sec(s)} --> {sec(e)}\n{t}\n\n")
    for s, e, t in cues:
        print("%7.2f %7.2f  %s" % (s, e, t.replace("\n", " / ")[:70]))


write(S + "/intro_new.srt", build("intro", INTRO))
write(S + "/outro_new.srt", build("outro", OUTRO, dropafter=80))
