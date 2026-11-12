"""Make the subtitle file of an Order of the Flame campaign pack from the player's own voice clips.

The PC game ships no subtitle text, only the voice clips its cut scenes play. This tool exports those clips from the
player's install (editor/scripts/ootf-speech.ts), transcribes them on this machine with Whisper (faster-whisper, CPU, no
network after the model download) and writes an engine-neutral subtitle file: one line per clip with timed cues. The
converter reads it with `ootf-convert.ts --subtitles <file>` and stores the text in the pack.

    <venv>/python analysis/scripts/ootf_subtitles.py --ootf <OotF install dir> --out build/ootf-subtitles/english.json

    pip install faster-whisper numpy      (in a venv; the first run downloads the model, about 0.5 GB for small.en)

The result is game text made from game audio: it must stay out of the repository (write it under build/ or local/).
Review it before a release build: names are spelled by ear. The file is plain JSON and can be corrected by hand; a line
whose cues are emptied gets no subtitle.
"""

from __future__ import annotations

import argparse
import json
import re
import subprocess
import sys
import wave
from collections.abc import Iterable, Sequence
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
FORMAT = "drakan-subtitles"
VERSION = 1
PAGE_CHARS = 84  # two lines of the engine's subtitle bar
# names and words of the game, given to the recogniser as context so it spells them
PROMPT = (
    "Drakan: Order of the Flame. Rynn, Arokh, Delon, Navaros, Heron, Atimar, Wartoks, Grulls, Succubus, Jade, Zeggoro, "
    "Ranwulf, Rimril, Alwarren, the Dark Union, the Order of the Flame, the Bond, Drakan, Ravenshold, Shilla."
)
# what a speech model writes for noise, music and silence
NOISE = re.compile(
    r"^\W*(thanks? (you|for watching).*|you|bye\W*|okay\W*|oh\W*|ah+\W*|uh+\W*|hm+\W*|\[.*\]|\(.*\)|\u266a.*|\.+)?\W*$",
    re.IGNORECASE,
)


def clean_text(text: str) -> str:
    """Plain text for the engine's font: straight quotes, no dashes that would read as an em-dash, one space."""
    text = text.replace("\u2018", "'").replace("\u2019", "'").replace("\u201c", '"').replace("\u201d", '"')
    text = re.sub(r"\s*[\u2013\u2014\u2015]\s*", ", ", text).replace("\u2026", "...").replace("\\", "/")
    return re.sub(r"\s+", " ", text).strip()


def build_cues(
    words: Sequence[tuple[float, float, str]], max_chars: int = PAGE_CHARS, pause: float = 0.7
) -> list[dict]:
    """Group timed words (start, end, text) into cues of at most `max_chars`: a cue ends at a sentence end, at a pause
    of `pause` seconds or when the next word no longer fits; a break inside a sentence prefers the last comma."""
    cues: list[dict] = []
    cur: list[tuple[float, float, str]] = []

    def flush() -> None:
        if cur:
            text = clean_text(" ".join(w[2].strip() for w in cur))
            if text:
                cues.append({"start": round(cur[0][0], 2), "end": round(cur[-1][1], 2), "text": text})
            cur.clear()

    def length(ws: Iterable[tuple[float, float, str]]) -> int:
        return len(clean_text(" ".join(w[2].strip() for w in ws)))

    for w in words:
        if not w[2].strip():
            continue
        if cur and w[0] - cur[-1][1] >= pause:
            flush()
        if cur and length([*cur, w]) > max_chars:
            # break at the last comma or clause mark when it leaves a reasonable first part
            cut = max((i for i, c in enumerate(cur[:-1]) if c[2].strip().endswith((",", ";", ":"))), default=-1)
            if cut >= 0 and length(cur[: cut + 1]) >= max_chars // 3:
                rest = cur[cut + 1 :]
                del cur[cut + 1 :]
                flush()
                cur.extend(rest)
            else:
                flush()
        cur.append(w)
        if w[2].strip().endswith((".", "!", "?")) and length(cur) >= max_chars // 3:
            flush()
    flush()
    return cues


PROMPT_WORDS = {w.lower() for w in re.findall(r"[A-Za-z]+", PROMPT)} - {"the", "of", "order", "flame", "dark", "bond"}


def is_speech(text: str, duration: float, no_speech: float, logprob: float, name: str = "") -> bool:
    """Keep a transcript only when it reads as a spoken line. A speech model writes something for every clip: for an
    effect, a growl or music it repeats one word, echoes the names it was given as context or spells a noise."""
    text = clean_text(text)
    if not text or NOISE.match(text):
        return False
    if no_speech > 0.6 or logprob < -1.0:
        return False
    words = re.findall(r"[A-Za-z']+", text.lower())
    if not words:
        return False
    if len(words) / max(duration, 0.2) > 6.0:  # nobody speaks faster; a longer text on a short clip is invented
        return False
    if any(re.search(r"(.)\1\1\1", w) for w in words):  # "grrrr", "vroooh": a spelled noise
        return False
    if len(words) >= 4 and len(set(words)) / len(words) <= 0.5:  # one phrase over and over
        return False
    echoed = sum(1 for w in words if w in PROMPT_WORDS)
    if len(words) >= 3 and echoed / len(words) > 0.5:  # the context names read back
        return False
    if len(words) == 1:
        # One word on its own is kept only when the clip's name in the sound bank says the same ("7- aroch!").
        tokens = re.findall(r"[a-z]+", name.lower())
        return len(words[0]) >= 2 and any(t.startswith(words[0][:3]) for t in tokens)
    return True


def read_wav(path: Path):
    import numpy as np

    with wave.open(str(path), "rb") as w:
        rate = w.getframerate()
        pcm = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").astype("float32") / 32768.0
    if rate != 16000 and len(pcm):
        n = max(1, round(len(pcm) * 16000 / rate))
        pcm = np.interp(np.linspace(0, len(pcm) - 1, n), np.arange(len(pcm)), pcm).astype("float32")
    return pcm


def transcribe(work: Path, model_name: str, language: str | None, min_seconds: float) -> list[dict]:
    from faster_whisper import WhisperModel

    clips = json.loads((work / "manifest.json").read_text(encoding="utf-8"))["clips"]
    model = WhisperModel(model_name, device="cpu", compute_type="int8")
    lines: list[dict] = []
    for i, clip in enumerate(clips):
        if clip["loop"] or clip["duration"] < min_seconds:
            continue
        audio = read_wav(work / clip["file"])
        segments, _ = model.transcribe(
            audio,
            language=language,
            word_timestamps=True,
            condition_on_previous_text=False,
            initial_prompt=PROMPT,
            beam_size=5,
            temperature=0.0,
        )
        segs = list(segments)
        text = " ".join(s.text.strip() for s in segs)
        if not segs:
            continue
        no_speech = max(s.no_speech_prob for s in segs)
        logprob = min(s.avg_logprob for s in segs)
        if not is_speech(text, clip["duration"], no_speech, logprob, clip["name"]):
            continue
        words = [(w.start, w.end, w.word) for s in segs for w in (s.words or [])]
        cues = build_cues(words) if words else [{"start": 0.0, "end": clip["duration"], "text": clean_text(text)}]
        if cues:
            lines.append(
                {
                    "bank": clip["bank"],
                    "id": clip["id"],
                    "name": clip["name"],
                    "duration": clip["duration"],
                    "cues": cues,
                }
            )
        if i % 25 == 0:
            print(f"  {i + 1} / {len(clips)} clips, {len(lines)} lines", flush=True)
    return lines


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--ootf", help="Order of the Flame install folder (clips are exported from it)")
    ap.add_argument("--work", type=Path, help="folder for the exported clips (default: <out folder>/speech)")
    ap.add_argument("--out", type=Path, required=True, help="subtitle file to write (keep it under build/ or local/)")
    ap.add_argument(
        "--model", default="small.en", help="faster-whisper model (small.en; small / medium for other languages)"
    )
    ap.add_argument("--language", default="english", help="language name stored in the file (drakan.ini spelling)")
    ap.add_argument("--min-seconds", type=float, default=0.6, help="shorter clips are effects, not lines")
    ap.add_argument("--refilter", type=Path, help="apply the speech filter to an existing subtitle file instead")
    a = ap.parse_args()
    if a.refilter:
        doc = json.loads(a.refilter.read_text(encoding="utf-8"))
        kept = [
            line
            for line in doc["lines"]
            if is_speech(
                " ".join(c["text"] for c in line["cues"]), line.get("duration", 1.0), 0.0, 0.0, line.get("name", "")
            )
        ]
        print(f"kept {len(kept)} of {len(doc['lines'])} line(s)")
        doc["lines"] = kept
        a.out.parent.mkdir(parents=True, exist_ok=True)
        a.out.write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
        return 0
    work = a.work or a.out.parent / "speech"
    if a.ootf:
        work.mkdir(parents=True, exist_ok=True)
        cmd = ["node", "--experimental-transform-types", "--no-warnings", "editor/scripts/ootf-speech.ts"]
        subprocess.run([*cmd, "--ootf", a.ootf, "--out", str(work.resolve())], cwd=ROOT, check=True)
    if not (work / "manifest.json").is_file():
        raise SystemExit(f"{work}/manifest.json not found: pass --ootf <install folder>")
    codes = {"english": "en", "german": "de", "french": "fr", "italian": "it", "spanish": "es"}
    lines = transcribe(work, a.model, codes.get(a.language), a.min_seconds)
    a.out.parent.mkdir(parents=True, exist_ok=True)
    doc = {"format": FORMAT, "version": VERSION, "language": a.language, "lines": lines}
    a.out.write_text(json.dumps(doc, ensure_ascii=False, indent=1), encoding="utf-8")
    print(f"wrote {len(lines)} line(s) to {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
