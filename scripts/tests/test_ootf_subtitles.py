"""The subtitle tool's pure parts on synthetic words: no game audio, no game text, no speech model."""

import importlib.util
import itertools
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

TOOL = pathlib.Path(__file__).resolve().parents[2] / "analysis" / "scripts" / "ootf_subtitles.py"
SPEC = importlib.util.spec_from_file_location("ootf_subtitles", TOOL)
SUB = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(SUB)


def words(text: str, start: float = 0.0, step: float = 0.4) -> list[tuple[float, float, str]]:
    return [(start + i * step, start + i * step + 0.3, " " + w) for i, w in enumerate(text.split())]


class BuildCues(unittest.TestCase):
    def test_sentences_become_cues_with_their_times(self):
        cues = SUB.build_cues(words("One short test sentence stands here. Another one follows right after it."))
        self.assertEqual(
            [c["text"] for c in cues], ["One short test sentence stands here.", "Another one follows right after it."]
        )
        self.assertEqual(cues[0]["start"], 0.0)
        self.assertAlmostEqual(cues[1]["start"], 6 * 0.4, places=2)
        self.assertGreater(cues[1]["end"], cues[1]["start"])

    def test_no_cue_is_longer_than_a_page(self):
        text = " ".join(f"word{i}," if i % 7 == 6 else f"word{i}" for i in range(60))
        cues = SUB.build_cues(words(text), max_chars=60)
        self.assertGreater(len(cues), 3)
        self.assertTrue(all(len(c["text"]) <= 60 for c in cues))
        self.assertEqual(" ".join(c["text"] for c in cues), text)
        for a, b in itertools.pairwise(cues):
            self.assertLessEqual(a["end"], b["start"] + 1e-6)

    def test_a_pause_ends_a_cue(self):
        timed = [*words("before the pause"), *words("after it", start=5.0)]
        self.assertEqual([c["text"] for c in SUB.build_cues(timed)], ["before the pause", "after it"])

    def test_text_is_made_plain(self):
        self.assertEqual(SUB.clean_text("\u201cWait\u201d \u2014 it\u2019s  here\u2026"), '"Wait", it\'s here...')
        self.assertNotIn("\\", SUB.clean_text("a\\b"))


class SpeechFilter(unittest.TestCase):
    def test_a_spoken_line_is_kept(self):
        self.assertTrue(
            SUB.is_speech("We should leave this place before dark.", 3.0, 0.1, -0.3, "07 - we should leave")
        )

    def test_noise_is_dropped(self):
        for text, name in [
            ("Grrrrrrrrrrrr", "Growl_2"),  # a spelled noise
            ("Go go go go go go go go", "Chant_1"),  # one word over and over
            ("Rynn, Arokh, Delon, Navaros.", "Gust_3"),  # the context names read back
            ("Thank you.", "Wind_1"),  # what a speech model writes for silence
            ("Boom!", "Blast_1"),  # one word the clip's name does not say
            ("", "Empty"),
        ]:
            self.assertFalse(SUB.is_speech(text, 2.0, 0.1, -0.3, name), text)

    def test_a_single_word_needs_the_clip_name(self):
        self.assertTrue(SUB.is_speech("Arokh!", 0.8, 0.1, -0.3, "7- aroch!"))
        self.assertFalse(SUB.is_speech("Arokh!", 0.8, 0.1, -0.3, "Navaros Roar part 1"))

    def test_unsure_or_impossible_transcripts_are_dropped(self):
        line = "We should leave this place before dark."
        self.assertFalse(SUB.is_speech(line, 3.0, 0.9, -0.3, ""))  # the model heard no speech
        self.assertFalse(SUB.is_speech(line, 3.0, 0.1, -1.4, ""))  # low confidence
        self.assertFalse(SUB.is_speech(line, 0.5, 0.1, -0.3, ""))  # seven words in half a second


class Refilter(unittest.TestCase):
    def test_refilter_keeps_the_file_format(self):
        doc = {
            "format": SUB.FORMAT,
            "version": SUB.VERSION,
            "language": "english",
            "lines": [
                {
                    "bank": "a/b",
                    "id": 1,
                    "name": "line one",
                    "duration": 3.0,
                    "cues": [{"start": 0, "end": 3, "text": "A plain spoken test line."}],
                },
                {
                    "bank": "a/b",
                    "id": 2,
                    "name": "Growl",
                    "duration": 2.0,
                    "cues": [{"start": 0, "end": 2, "text": "Grrrrrrrr"}],
                },
            ],
        }
        with tempfile.TemporaryDirectory() as tmp:
            src, out = pathlib.Path(tmp) / "in.json", pathlib.Path(tmp) / "out.json"
            src.write_text(json.dumps(doc), encoding="utf-8")
            subprocess.run(
                [sys.executable, "-I", str(TOOL), "--refilter", str(src), "--out", str(out)],
                check=True,
                capture_output=True,
            )
            got = json.loads(out.read_text(encoding="utf-8"))
        self.assertEqual(got["format"], "drakan-subtitles")
        self.assertEqual([line["id"] for line in got["lines"]], [1])


class ReviewedFiles(unittest.TestCase):
    def test_existing_lines_win_over_a_fresh_transcription(self):
        kept = [
            {"bank": "a/b", "id": 1, "cues": [{"start": 0, "end": 1, "text": "corrected"}]},
            {"bank": "a/b", "id": 2, "cues": []},
        ]
        fresh = [
            {"bank": "a/b", "id": 1, "cues": [{"start": 0, "end": 1, "text": "misheard"}]},
            {"bank": "a/b", "id": 2, "cues": [{"start": 0, "end": 1, "text": "noise"}]},
            {"bank": "a/b", "id": 3, "cues": [{"start": 0, "end": 1, "text": "new"}]},
        ]
        merged = SUB.merge_reviewed(kept, fresh)
        self.assertEqual(
            [(m["id"], [c["text"] for c in m["cues"]]) for m in merged], [(1, ["corrected"]), (2, []), (3, ["new"])]
        )

    def test_review_text_lists_flagged_lines_and_whole_banks(self):
        lines = [
            {
                "bank": "a/sure",
                "id": 1,
                "name": "one",
                "duration": 2.0,
                "confidence": -0.2,
                "cues": [{"start": 0.0, "end": 2.0, "text": "A sure line."}],
            },
            {
                "bank": "a/sure",
                "id": 2,
                "name": "two",
                "duration": 2.0,
                "confidence": -0.7,
                "review": True,
                "cues": [{"start": 0.5, "end": 2.0, "text": "An unsure line."}],
            },
            {
                "bank": "a/story",
                "id": 3,
                "name": "three",
                "duration": 2.0,
                "confidence": -0.1,
                "cues": [{"start": 0.0, "end": 2.0, "text": "A story line."}],
            },
        ]
        text = SUB.review_text(lines, always="story")
        self.assertNotIn("A sure line.", text)
        self.assertIn("a/sure #2 (two, 2.0 s, confidence -0.70)", text)
        self.assertIn("  0.50  An unsure line.", text)
        self.assertIn("A story line.", text)


class ModelNotice(unittest.TestCase):
    def test_a_missing_model_is_named_with_source_licence_and_size(self):
        with tempfile.TemporaryDirectory() as tmp:
            notice = SUB.model_notice("small.en", pathlib.Path(tmp))
            self.assertIn("faster-whisper-small.en", notice)
            self.assertIn("https://huggingface.co/Systran/faster-whisper-small.en", notice)
            self.assertIn("MIT", notice)
            self.assertIn("464 MiB", notice)
            snap = pathlib.Path(tmp) / "models--Systran--faster-whisper-small.en" / "snapshots" / "abc"
            snap.mkdir(parents=True)
            (snap / "model.bin").write_bytes(b"x")
            self.assertIsNone(SUB.model_notice("small.en", pathlib.Path(tmp)))
            self.assertIsNone(SUB.model_notice(tmp, pathlib.Path(tmp)))  # a local model folder

    def test_nothing_is_downloaded_without_the_flag(self):
        with tempfile.TemporaryDirectory() as tmp:
            work = pathlib.Path(tmp) / "speech"
            work.mkdir()
            (work / "manifest.json").write_text('{"clips": []}', encoding="utf-8")
            done = subprocess.run(
                [
                    sys.executable,
                    "-I",
                    str(TOOL),
                    "--work",
                    str(work),
                    "--out",
                    str(pathlib.Path(tmp) / "o.json"),
                    "--model",
                    "no-such-model-name",
                ],
                capture_output=True,
                text=True,
            )
            self.assertNotEqual(done.returncode, 0)
            self.assertIn("is not in", done.stdout)
            self.assertIn("nothing was downloaded", done.stderr + done.stdout)
            self.assertFalse((pathlib.Path(tmp) / "o.json").exists())


if __name__ == "__main__":
    unittest.main()
