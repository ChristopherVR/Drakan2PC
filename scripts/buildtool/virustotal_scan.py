"""Scan release downloads with VirusTotal and write the "VirusTotal scan" section of the release notes.

Usage (release-scan.yml):
    python scripts/buildtool/virustotal_scan.py --body body.md --output new-body.md FILE...

Needs VT_API_KEY (a free virustotal.com key). Each file is looked up by SHA-256 first; only a file VirusTotal has never
seen is uploaded. Every result links to the file's permanent report page, so a player can compare the hash of their own
download with the one VirusTotal scanned. A file whose analysis did not finish, or that could not be sent, is listed as
such: it is never reported as clean. Writes vt_detected.txt ("true"/"false") for the workflow's final gate.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

API = "https://www.virustotal.com/api/v3"
SECTION = "## VirusTotal scan"
# The free API allows four requests a minute; pace every call so a long release never trips the limit.
REQUEST_GAP = 16.0
SIMPLE_UPLOAD_LIMIT = 32 * 1024 * 1024


class Client:
    def __init__(self, key: str, gap: float = REQUEST_GAP):
        self.key = key
        self.gap = gap
        self.last = None

    def _pace(self):
        if self.last is not None:
            wait = self.gap - (time.monotonic() - self.last)
            if wait > 0:
                time.sleep(wait)
        self.last = time.monotonic()

    def get(self, path: str):
        """Return the decoded JSON body, or None when VirusTotal answers 404 (file unknown)."""
        self._pace()
        request = urllib.request.Request(API + path, headers={"x-apikey": self.key})
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)
        except urllib.error.HTTPError as error:
            if error.code == 404:
                return None
            raise

    def upload(self, path: Path) -> str:
        """Upload one file and return its analysis id. Large files go through a one-off upload address."""
        url = API + "/files"
        if path.stat().st_size > SIMPLE_UPLOAD_LIMIT:
            url = self.get("/files/upload_url")["data"]
        self._pace()
        # curl streams the file; urllib would need the whole multipart body in memory.
        result = subprocess.run(
            ["curl", "-sS", "--fail-with-body", "-H", f"x-apikey: {self.key}", "-F", f"file=@{path}", url],
            capture_output=True,
            text=True,
            check=True,
        )
        return json.loads(result.stdout)["data"]["id"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def describe(stats: dict) -> tuple[str, bool]:
    """A one-line verdict from VirusTotal's engine counts, and whether any engine flagged the file."""
    malicious = stats.get("malicious", 0)
    suspicious = stats.get("suspicious", 0)
    hits = malicious + suspicious
    # Engines that timed out or cannot read the file type are left out of the count.
    checked = hits + stats.get("undetected", 0) + stats.get("harmless", 0)
    if hits:
        return f"flagged by {hits} of {checked} engines ({malicious} malicious, {suspicious} suspicious)", True
    return f"clean, none of the {checked} engines that checked it flagged it", False


def known_report(client: Client, digest: str):
    """The finished analysis VirusTotal already holds for this exact file, if any."""
    report = client.get(f"/files/{digest}")
    if report is None:
        return None
    attributes = report["data"]["attributes"]
    stats = attributes.get("last_analysis_stats")
    if not stats or not attributes.get("last_analysis_date"):
        return None
    return stats


def scan(files: list[Path], client: Client, timeout: float = 3000) -> tuple[list[tuple[str, str, str]], bool]:
    """Return (name, report url, verdict) per file, in input order, and whether anything was flagged."""
    verdicts: dict[str, str] = {}
    digests: dict[str, str] = {}
    pending: dict[str, str] = {}
    detected = False
    for path in files:
        digest = sha256(path)
        digests[path.name] = digest
        try:
            stats = known_report(client, digest)
            if stats is not None:
                verdicts[path.name], hit = describe(stats)
                detected |= hit
                continue
            pending[path.name] = client.upload(path)
            print(f"Uploaded {path.name}")
        except (urllib.error.URLError, subprocess.CalledProcessError, OSError, KeyError, ValueError) as error:
            print(f"::warning::VirusTotal could not take {path.name}: {error}")
            verdicts[path.name] = "could not be sent to VirusTotal this time; check its SHA-256 on virustotal.com"

    deadline = time.monotonic() + timeout
    while pending and time.monotonic() < deadline:
        for name, analysis in list(pending.items()):
            if time.monotonic() >= deadline:
                break
            try:
                attributes = client.get(f"/analyses/{analysis}")["data"]["attributes"]
            except (urllib.error.URLError, OSError, KeyError, TypeError, ValueError):
                continue  # A failed poll is retried; it never turns into a clean verdict.
            if attributes.get("status") == "completed":
                verdicts[name], hit = describe(attributes.get("stats", {}))
                detected |= hit
                del pending[name]
    for name in pending:
        verdicts[name] = "no result yet (the scan was too slow to finish); open its link for the latest verdict"

    rows = [(p.name, f"https://www.virustotal.com/gui/file/{digests[p.name]}", verdicts[p.name]) for p in files]
    return rows, detected


def section(rows: list[tuple[str, str, str]]) -> str:
    lines = [SECTION, "", "Automated multi-engine scan of the downloads above:", ""]
    lines += [f"- [`{name}`]({url}): {verdict}" for name, url, verdict in rows]

    return "\n".join(lines) + "\n"


def replace_section(body: str, new_section: str) -> str:
    """Drop any earlier VirusTotal section (it is always the last one) and append the fresh one."""
    kept = body.split(SECTION, 1)[0].rstrip()
    return (kept + "\n\n" if kept else "") + new_section


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("files", nargs="+", type=Path)
    parser.add_argument("--body", type=Path, required=True, help="current release notes")
    parser.add_argument("--output", type=Path, required=True, help="where to write the updated release notes")
    args = parser.parse_args(argv)
    client = Client(os.environ["VT_API_KEY"])
    rows, detected = scan(args.files, client)
    text = section(rows)
    print(text)
    args.output.write_text(replace_section(args.body.read_text(encoding="utf-8"), text), encoding="utf-8")
    Path("vt_detected.txt").write_text("true" if detected else "false", encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main())
