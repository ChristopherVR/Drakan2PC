import importlib.util
import tempfile
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import patch

SCRIPT = Path(__file__).resolve().parents[1] / "buildtool" / "virustotal_scan.py"
spec = importlib.util.spec_from_file_location("virustotal_scan", SCRIPT)
vt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vt)


class FakeClient:
    """Answers GET paths from a table (a list is consumed in order) and records uploads."""

    def __init__(self, answers, upload_id="analysis-1", upload_error=None):
        self.answers = answers
        self.upload_id = upload_id
        self.upload_error = upload_error
        self.uploads = []
        self.calls = []

    def get(self, path):
        self.calls.append(path)
        for prefix, answer in self.answers.items():
            if path.startswith(prefix):
                if isinstance(answer, list):
                    answer = answer.pop(0)
                if isinstance(answer, Exception):
                    raise answer
                return answer
        raise AssertionError(f"unexpected request {path}")

    def upload(self, path):
        self.uploads.append(path.name)
        if self.upload_error:
            raise self.upload_error
        return self.upload_id


def report(stats):
    return {"data": {"attributes": {"last_analysis_stats": stats, "last_analysis_date": 1}}}


def analysis(status, stats=None):
    return {"data": {"attributes": {"status": status, "stats": stats or {}}}}


class VirusTotalScanTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.file = Path(self.temp.name) / "DrakanPC-Builder.zip"
        self.file.write_bytes(b"kit")
        self.digest = vt.sha256(self.file)

    def tearDown(self):
        self.temp.cleanup()

    def test_known_file_reuses_report_without_upload(self):
        client = FakeClient({"/files/": report({"undetected": 60, "timeout": 3, "type-unsupported": 9})})
        rows, detected = vt.scan([self.file], client)
        self.assertEqual(client.uploads, [])
        self.assertFalse(detected)
        self.assertEqual(rows[0][1], f"https://www.virustotal.com/gui/file/{self.digest}")
        self.assertIn("none of the 60 engines", rows[0][2])

    def test_unknown_file_is_uploaded_and_polled_until_completed(self):
        client = FakeClient(
            {
                "/files/": None,
                "/analyses/": [analysis("queued"), analysis("completed", {"malicious": 2, "undetected": 56})],
            }
        )
        rows, detected = vt.scan([self.file], client)
        self.assertEqual(client.uploads, [self.file.name])
        self.assertTrue(detected)
        self.assertIn("flagged by 2 of 58 engines (2 malicious, 0 suspicious)", rows[0][2])

    def test_suspicious_counts_as_detection(self):
        _, detected = vt.describe({"suspicious": 1, "undetected": 10})
        self.assertTrue(detected)

    def test_unfinished_analysis_is_never_called_clean(self):
        client = FakeClient({"/files/": None})
        rows, detected = vt.scan([self.file], client, timeout=0)
        self.assertFalse(detected)
        self.assertIn("no result yet", rows[0][2])

    def test_failed_poll_is_never_called_clean(self):
        client = FakeClient({"/files/": None, "/analyses/": urllib.error.URLError("down")})
        with patch.object(vt.time, "monotonic", side_effect=[0, 0, 0, 2]):
            rows, _ = vt.scan([self.file], client, timeout=1)
        self.assertIn("no result yet", rows[0][2])

    def test_refused_upload_is_named_not_dropped(self):
        client = FakeClient({"/files/": None}, upload_error=OSError("refused"))
        rows, _ = vt.scan([self.file], client)
        self.assertIn("could not be sent", rows[0][2])

    def test_section_replaces_previous_copy(self):
        body = "Changes\n\nDownload notes\n\n## VirusTotal scan\n\nold results\n"
        new = vt.section([("a.zip", "https://example/a", "clean")])
        updated = vt.replace_section(vt.replace_section(body, new), new)
        self.assertEqual(updated.count(vt.SECTION), 1)
        self.assertNotIn("old results", updated)
        self.assertTrue(updated.startswith("Changes\n\nDownload notes\n\n## VirusTotal scan"))


if __name__ == "__main__":
    unittest.main()
