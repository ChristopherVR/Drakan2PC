"""audio_tone_check finds a parked 11 kHz tone and ignores plain noise (synthetic signals only)."""

import sys
import unittest
from pathlib import Path

import numpy as np

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "harness"))
import audio_tone_check as tone

RATE = 48000


class ToneCheckTests(unittest.TestCase):
    def signal(self, hz=None, seconds=8):
        t = np.arange(RATE * seconds) / RATE
        noise = np.random.default_rng(1).normal(0, 0.05, len(t))
        return noise if hz is None else noise + 0.02 * np.sin(2 * np.pi * hz * t)

    def test_parked_tone_is_found(self):
        found = tone.sustained(tone.peaks(self.signal(11000), RATE, 3000, 25))
        self.assertEqual([round(f) for f, _, _ in found], [11000])

    def test_noise_is_clean(self):
        self.assertEqual(tone.sustained(tone.peaks(self.signal(), RATE, 3000, 25)), [])

    def test_short_blip_is_not_sustained(self):
        data = self.signal()
        t = np.arange(RATE) / RATE
        data[RATE * 3 : RATE * 4] += 0.05 * np.sin(2 * np.pi * 9000 * t)
        self.assertEqual(tone.sustained(tone.peaks(data, RATE, 3000, 25)), [])


if __name__ == "__main__":
    unittest.main()
