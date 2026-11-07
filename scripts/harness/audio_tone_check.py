"""Find sustained narrow high-pitched tones in a game audio dump (issue 1: the 11 kHz idle beep).

    DRAKAN_AUDIO_DUMP=<run>/audio.wav  (see docs/reference/env-flags.md), then
    python scripts/harness/audio_tone_check.py <run>/audio.wav [--min-hz 3000] [--db 25]

A tone is a spectral peak at or above --min-hz that stands --db dB above the average of the +-100 Hz around it in its
1 s window and stays within 50 Hz of itself for 3 or more consecutive windows. Music and speech move around; a parked or looping voice does not.
Exit 1 when a tone is found.
"""

from __future__ import annotations

import argparse
import wave
from pathlib import Path

import numpy as np


def load(path: Path) -> tuple[np.ndarray, int]:
    with wave.open(str(path), "rb") as w:
        if w.getsampwidth() != 2:
            raise SystemExit("expected 16-bit PCM")
        rate, channels = w.getframerate(), w.getnchannels()
        data = np.frombuffer(w.readframes(w.getnframes()), dtype="<i2").reshape(-1, channels).astype(np.float64)
    return data.mean(axis=1) / 32768.0, rate


def peaks(signal: np.ndarray, rate: int, min_hz: float, db: float) -> list[list[float]]:
    """Per 1 s window, the frequencies of narrow peaks above min_hz (db above the local +-100 Hz average)."""
    out: list[list[float]] = []
    window = np.hanning(rate)
    freqs = np.fft.rfftfreq(rate, 1 / rate)
    kernel = np.ones(201) / 201
    for start in range(0, len(signal) - rate + 1, rate):
        chunk = signal[start : start + rate]
        if np.max(np.abs(chunk)) < 1e-4:
            out.append([])
            continue
        power = 20 * np.log10(np.abs(np.fft.rfft(chunk * window)) + 1e-9)
        local = np.convolve(power, kernel, mode="same")
        mask = (freqs >= min_hz) & (power > local + db)
        found = [
            float(freqs[i])
            for i in np.where(mask)[0]
            if power[i] >= power[max(i - 1, 0)] and power[i] >= power[min(i + 1, len(power) - 1)]
        ]
        out.append(found)
    return out


def sustained(per_window: list[list[float]], need: int = 3, tolerance: float = 50.0) -> list[tuple[float, int, int]]:
    tones = []
    for start, freqs in enumerate(per_window):
        for f in freqs:
            length = 1
            while start + length < len(per_window) and any(abs(g - f) <= tolerance for g in per_window[start + length]):
                length += 1
            if length >= need and not (start > 0 and any(abs(g - f) <= tolerance for g in per_window[start - 1])):
                tones.append((f, start, length))
    return tones


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("wav", type=Path)
    parser.add_argument("--min-hz", type=float, default=3000.0)
    parser.add_argument("--db", type=float, default=25.0)
    args = parser.parse_args()
    signal, rate = load(args.wav)
    seconds = len(signal) / rate
    silent = sum(1 for i in range(0, len(signal) - rate + 1, rate) if np.max(np.abs(signal[i : i + rate])) < 1e-4)
    tones = sustained(peaks(signal, rate, args.min_hz, args.db))
    print(f"{args.wav}: {seconds:.1f} s at {rate} Hz, {silent} silent seconds, peak {np.max(np.abs(signal)):.3f}")
    for f, start, length in tones[:10]:
        print(f"  tone {f:.0f} Hz from {start} s for {length} s")
    print("TONE FOUND" if tones else "no sustained tone above %.0f Hz" % args.min_hz)
    return 1 if tones else 0


if __name__ == "__main__":
    raise SystemExit(main())
