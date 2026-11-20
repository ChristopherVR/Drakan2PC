# Antivirus detections of DrakanPC-Builder.zip (issue #14)

Report: Windows Defender removed `DrakanPC-Builder.zip` on download as a severe trojan. The reporter's VirusTotal
screenshot is the v0.1.31 zip (SHA-256 `02d06590...05d2e`, matching that release's `SHA256SUMS.txt`): 3 of 58 engines,
Avast and AVG `Other:Malware-gen [Trj]`, WithSecure `Trojan.TR/Malware`. Defender is not among the VirusTotal hits.

## What is in the zip

2623 entries, no PE files at all: C++/TS/Python sources, 27 `.ps1`, a few `.cmd`, JS, and 120 MB of PNG art. So every
hit is a heuristic on scripts or on the archive as a whole, not on a binary.

Local Windows Defender (signatures 1.463.389.0, updated 2026-11-21) finds nothing in the zip or in the extracted tree.
The reporter's removal on download is most likely a cloud-delivered machine-learning verdict on a fresh, unsigned,
low-reputation file (those `!ml` style detections do not reproduce in an offline scan and often clear as the file gains
reputation or after a false-positive submission).

## Root cause (bisected on VirusTotal)

The first guess (downloader-shaped PowerShell, below) was wrong: a kit with those patterns removed still scored 3 of 60
with the same three detection names. Splitting the kit and uploading each part found the file:

| Upload | Result |
| --- | --- |
| kit without `.claude/` | still flagged (F-Secure; Avast/AVG timed out) |
| `.claude/skills/.../SKILL.md` alone (the `contains-skill` tag) | clean, 0 of 64 |
| each top-level folder | all clean except `scripts/` (4 of 63) |
| each `scripts/` subfolder | all clean except the 15 loose files (5 of 65) |
| each of those 15 files alone | only `scripts/launchers/Play.cmd`: 5 of 63 (Avast, AVG, Avira, F-Secure, Cynet) |

The old `Play.cmd` (`setlocal`, `cd /d "%~dp0"`, `start "" "%~dp0drakan.exe"`, `endlocal`) matches a generic batch
dropper signature. `Play-Original30.cmd`, the same commands plus `set` lines, is clean. The rewrite (`pushd "%~dp0"`,
`start "Drakan" drakan.exe %*`, `popd`) is clean on 64 engines, and the full kit with only that change (and `.claude/`
dropped) is clean on 59, with all five former detectors reporting undetected.

To repeat a check: put a key in `local/vt_api_key.txt` and run `scripts/buildtool/virustotal_scan.py` on the zip.

## Downloader patterns (hardening, not the cause)
The kit has the shape generic "trojan downloader" rules look for:

- `Build-Drakan.cmd` at the archive root runs `pwsh -NoProfile -ExecutionPolicy Bypass -File ...`.
- `Build-Drakan.ps1` `Invoke-WebRequest`s an `.exe` (Microsoft's `vs_BuildTools.exe`) into `%TEMP%` and
  `Start-Process`es it (it does check the Authenticode signature first).
- `extras.py` builds an `Invoke-WebRequest` PowerShell command line; developer harness scripts in `scripts/harness/`
  and `scripts/tests/` start processes with `-WindowStyle Hidden`. None of these are needed by players.
- Nothing is code-signed, and every release is a new hash with no reputation.

## What changed

- Defender gate before upload (`Scan-ReleaseDefender.ps1`) and VirusTotal verdicts in the release notes
  (`release-scan.yml`, `VT_API_KEY` secret).
- `Build-Drakan.ps1` installs the Build Tools with winget (or the existing Visual Studio Installer for a modify) instead
  of `Invoke-WebRequest` + `Start-Process` of `vs_BuildTools.exe`.
- `extras.py` no longer builds a PowerShell `Invoke-WebRequest` command line as a download fallback; `curl.exe` only.
- The kit leaves out every `.ps1` outside `scripts/buildtool/`; `test_build_job.py` no longer launches with a hidden
  window. `test_kit_antivirus_patterns.py` keeps these patterns out of shipped scripts.
- The player-facing antivirus note is in the release notes (`kit/RELEASE-NOTES.md`), not the README.

- `Play.cmd` rewritten (the actual trigger, above); `.claude/` left out of the kit.

From `c442dc7b` the zip also carries a prebuilt `ps2_recomp.exe` and FFmpeg, so the next release's zip has binaries for
the first time; its VirusTotal section is the check for that.

## Options not taken yet

- Authenticode-sign the Setup and scripts (removes most of the reputation problem; needs a certificate).
- Submit the release files as false positives to Microsoft (wdsi/filesubmission), Avast/AVG and WithSecure.